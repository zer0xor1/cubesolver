// Site-wide counters shown on the web page: how many people visited, and how
// many cubes the solver has solved for them.
//
// The counts live in memory and are sent to a CounterStore in the background,
// so a slow store never slows down a request. Stores:
//   - FileStore: a small text file ("visitors <n>" and "cubesSolved <n>").
//   - UpstashStore: a Redis database at Upstash, reached over HTTPS. Use it on
//     hosts without a disk (like Render's free plan), so the counts survive
//     restarts and deploys.
// Without a store, the counts are kept in memory only.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace cube::server {

struct Counts {
    uint64_t visitors = 0;
    uint64_t cubesSolved = 0;
};

class CounterStore {
public:
    virtual ~CounterStore() = default;
    // Adds delta to the stored counts and returns the new totals, so adding
    // {0, 0} reads them. Throws std::runtime_error if the store fails.
    virtual Counts add(const Counts& delta) = 0;
    virtual std::string describe() const = 0;
};

class FileStore : public CounterStore {
public:
    explicit FileStore(std::string file);
    Counts add(const Counts& delta) override;
    std::string describe() const override;

private:
    const std::string file_;
    Counts totals_;
};

// Uses Upstash's REST API: POST <url>/pipeline with INCRBY commands. The url
// may be https:// (needs a build with OpenSSL) or http:// (for tests).
class UpstashStore : public CounterStore {
public:
    UpstashStore(std::string url, std::string token, std::string keyPrefix = "cubesolver:");
    Counts add(const Counts& delta) override;
    std::string describe() const override;

private:
    std::string url_;  // without a trailing slash
    const std::string token_;
    const std::string prefix_;
};

class SiteCounters {
public:
    // Reads the starting counts from the store (if any), then saves changes
    // every flushEvery in a background thread, and once more on destruction.
    explicit SiteCounters(std::unique_ptr<CounterStore> store = nullptr,
                          std::chrono::milliseconds flushEvery = std::chrono::seconds(15));
    ~SiteCounters();
    SiteCounters(const SiteCounters&) = delete;
    SiteCounters& operator=(const SiteCounters&) = delete;

    void addVisitor();
    void addSolve();
    Counts counts() const;

    // {"visitors":<n>,"cubesSolved":<n>}
    std::string json() const;

    // Sends unsaved changes to the store now. Returns false if that failed;
    // the changes are kept and sent again next time.
    bool flush();

private:
    void run(std::chrono::milliseconds flushEvery);

    std::unique_ptr<CounterStore> store_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Counts saved_;    // totals the store last reported
    Counts pending_;  // changes not yet sent to the store
    bool stop_ = false;
    bool failing_ = false;   // print store errors once, not on every flush
    std::mutex flushMutex_;  // one flush at a time
    std::thread thread_;
};

}  // namespace cube::server
