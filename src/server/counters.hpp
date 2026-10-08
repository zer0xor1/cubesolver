// Site-wide counters shown on the web page: how many people visited, and how
// many cubes the solver has solved for them.
//
// If a file path is given, the counts are loaded from it at start-up and
// written back after every change, so they survive a server restart. The file
// is two lines: "visitors <n>" and "cubesSolved <n>".
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

namespace cube::server {

class SiteCounters {
public:
    explicit SiteCounters(std::string file = "");

    void addVisitor();
    void addSolve();
    uint64_t visitors() const;
    uint64_t cubesSolved() const;

    // {"visitors":<n>,"cubesSolved":<n>}
    std::string json() const;

private:
    void save();  // caller holds mutex_

    mutable std::mutex mutex_;
    const std::string file_;
    uint64_t visitors_ = 0;
    uint64_t cubesSolved_ = 0;
    bool warned_ = false;  // print a failed save only once
};

}  // namespace cube::server
