#include "counters.hpp"

#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "httplib/httplib.h"

namespace cube::server {

// ---------------------------------------------------------------- FileStore

FileStore::FileStore(std::string file) : file_(std::move(file)) {
    std::ifstream in(file_);
    std::string name;
    uint64_t value = 0;
    while (in >> name >> value) {
        if (name == "visitors") totals_.visitors = value;
        if (name == "cubesSolved") totals_.cubesSolved = value;
    }
}

Counts FileStore::add(const Counts& delta) {
    Counts next = totals_;
    next.visitors += delta.visitors;
    next.cubesSolved += delta.cubesSolved;
    // Write a temporary file, then rename it, so a crash never leaves half a file.
    const std::string tmp = file_ + ".tmp";
    bool ok = false;
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "visitors " << next.visitors << "\ncubesSolved " << next.cubesSolved << "\n";
        ok = static_cast<bool>(out);
    }
    if (!ok || std::rename(tmp.c_str(), file_.c_str()) != 0) {
        throw std::runtime_error("could not write " + file_);
    }
    totals_ = next;
    return totals_;
}

std::string FileStore::describe() const {
    return "file " + file_;
}

// ---------------------------------------------------------------- UpstashStore

namespace {

// Reads the numbers after each "result": in a reply like
// [{"result":12},{"result":3}]. Returns false if there are not exactly two.
bool parseTwoResults(const std::string& body, uint64_t& a, uint64_t& b) {
    uint64_t found[2] = {0, 0};
    int n = 0;
    const std::string key = "\"result\":";
    for (size_t pos = body.find(key); pos != std::string::npos; pos = body.find(key, pos)) {
        pos += key.size();
        size_t end = pos;
        while (end < body.size() && body[end] >= '0' && body[end] <= '9') ++end;
        if (end == pos || n == 2) return false;
        found[n++] = std::stoull(body.substr(pos, end - pos));
    }
    a = found[0];
    b = found[1];
    return n == 2;
}

}  // namespace

UpstashStore::UpstashStore(std::string url, std::string token, std::string keyPrefix)
    : url_(std::move(url)), token_(std::move(token)), prefix_(std::move(keyPrefix)) {
    while (!url_.empty() && url_.back() == '/') url_.pop_back();
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
    if (url_.rfind("https://", 0) == 0) {
        throw std::runtime_error("this server was built without HTTPS support (OpenSSL), so it cannot reach " + url_);
    }
#endif
}

Counts UpstashStore::add(const Counts& delta) {
    httplib::Client client(url_);
    client.set_connection_timeout(5, 0);
    client.set_read_timeout(5, 0);
    client.set_bearer_token_auth(token_);
    const std::string body = "[[\"INCRBY\",\"" + prefix_ + "visitors\",\"" + std::to_string(delta.visitors) +
                             "\"],[\"INCRBY\",\"" + prefix_ + "cubesSolved\",\"" + std::to_string(delta.cubesSolved) +
                             "\"]]";
    const auto res = client.Post("/pipeline", body, "application/json");
    if (!res) throw std::runtime_error("could not reach " + url_ + " (" + httplib::to_string(res.error()) + ")");
    if (res->status != 200) {
        throw std::runtime_error(url_ + " answered " + std::to_string(res->status) + ": " + res->body.substr(0, 200));
    }
    Counts totals;
    if (!parseTwoResults(res->body, totals.visitors, totals.cubesSolved)) {
        throw std::runtime_error("unexpected reply from " + url_ + ": " + res->body.substr(0, 200));
    }
    return totals;
}

std::string UpstashStore::describe() const {
    return "Upstash Redis at " + url_;
}

// ---------------------------------------------------------------- SiteCounters

SiteCounters::SiteCounters(std::unique_ptr<CounterStore> store, std::chrono::milliseconds flushEvery)
    : store_(std::move(store)) {
    if (!store_) return;
    try {
        saved_ = store_->add({});
    } catch (const std::exception& e) {
        // Start from zero; the first successful flush brings the real totals.
        std::fprintf(stderr, "Warning: could not read the counts from %s: %s\n", store_->describe().c_str(), e.what());
        failing_ = true;
    }
    thread_ = std::thread([this, flushEvery] { run(flushEvery); });
}

SiteCounters::~SiteCounters() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    thread_.join();
    flush();
}

void SiteCounters::run(std::chrono::milliseconds flushEvery) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!stop_) {
        wake_.wait_for(lock, flushEvery, [this] { return stop_; });
        if (stop_) break;
        // Only talk to the store when there is news, or to retry after a failure.
        if (pending_.visitors == 0 && pending_.cubesSolved == 0 && !failing_) continue;
        lock.unlock();
        flush();
        lock.lock();
    }
}

void SiteCounters::addVisitor() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++pending_.visitors;
}

void SiteCounters::addSolve() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++pending_.cubesSolved;
}

Counts SiteCounters::counts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {saved_.visitors + pending_.visitors, saved_.cubesSolved + pending_.cubesSolved};
}

std::string SiteCounters::json() const {
    const Counts c = counts();
    return "{\"visitors\":" + std::to_string(c.visitors) + ",\"cubesSolved\":" + std::to_string(c.cubesSolved) + "}";
}

bool SiteCounters::flush() {
    if (!store_) return true;
    std::lock_guard<std::mutex> flushLock(flushMutex_);
    Counts delta;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        delta = pending_;
        pending_ = {};
    }
    // Sending {0, 0} still refreshes the totals, which picks up changes made
    // by another copy of the server (for example during a deploy).
    try {
        const Counts totals = store_->add(delta);  // slow part, outside mutex_
        std::lock_guard<std::mutex> lock(mutex_);
        saved_ = totals;
        if (failing_) std::fprintf(stderr, "Saving counts to %s works again\n", store_->describe().c_str());
        failing_ = false;
        return true;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.visitors += delta.visitors;  // keep them for the next try
        pending_.cubesSolved += delta.cubesSolved;
        if (!failing_) std::fprintf(stderr, "Warning: could not save counts: %s\n", e.what());
        failing_ = true;
        return false;
    }
}

}  // namespace cube::server
