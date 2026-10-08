#include "counters.hpp"

#include <cstdio>
#include <fstream>
#include <utility>

namespace cube::server {

SiteCounters::SiteCounters(std::string file) : file_(std::move(file)) {
    if (file_.empty()) return;
    std::ifstream in(file_);
    std::string name;
    uint64_t value = 0;
    while (in >> name >> value) {
        if (name == "visitors") visitors_ = value;
        if (name == "cubesSolved") cubesSolved_ = value;
    }
}

void SiteCounters::addVisitor() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++visitors_;
    save();
}

void SiteCounters::addSolve() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++cubesSolved_;
    save();
}

uint64_t SiteCounters::visitors() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return visitors_;
}

uint64_t SiteCounters::cubesSolved() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cubesSolved_;
}

std::string SiteCounters::json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return "{\"visitors\":" + std::to_string(visitors_) + ",\"cubesSolved\":" + std::to_string(cubesSolved_) + "}";
}

void SiteCounters::save() {
    if (file_.empty()) return;
    // Write a temporary file, then rename it, so a crash never leaves half a file.
    const std::string tmp = file_ + ".tmp";
    bool ok = false;
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "visitors " << visitors_ << "\ncubesSolved " << cubesSolved_ << "\n";
        ok = static_cast<bool>(out);
    }
    ok = ok && std::rename(tmp.c_str(), file_.c_str()) == 0;
    if (!ok && !warned_) {
        std::fprintf(stderr, "Warning: could not save counters to %s\n", file_.c_str());
        warned_ = true;
    }
}

}  // namespace cube::server
