// Web server: JSON API + the 3D web demo.
//
//   cubesolver_server [--port 8080] [--host 127.0.0.1] [--web-dir path] [--data-file path]
//
// Hosting services pass the port in the PORT environment variable; it is used
// when --port is not given. CUBESOLVER_DATA_FILE works the same way for
// --data-file, the file that keeps the visitor and solve counts.
//
// To keep the counts on a host without a disk, set UPSTASH_REDIS_REST_URL and
// UPSTASH_REDIS_REST_TOKEN (from a free Upstash Redis database). They win over
// --data-file.
//
// SIGTERM and SIGINT (Ctrl+C) stop the server cleanly, so the last counts are
// saved before it exits. Hosting services send SIGTERM before a deploy.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef _WIN32
#include <pthread.h>

#include <csignal>
#endif

#include "api.hpp"
#include "cubesolver/tables.hpp"
#include "httplib/httplib.h"

namespace fs = std::filesystem;

namespace {

bool hasIndex(const fs::path& dir) {
    std::error_code ec;
    return !dir.empty() && fs::exists(dir / "index.html", ec);
}

// Prefer the source folder, so edits to web/ show up after a browser refresh.
// If it is gone (build folder moved), use the copy next to the executable.
std::string findWebDir(const char* argv0, const std::string& override) {
    if (!override.empty()) return override;
#ifdef CUBESOLVER_WEB_SOURCE_DIR
    if (hasIndex(CUBESOLVER_WEB_SOURCE_DIR)) return CUBESOLVER_WEB_SOURCE_DIR;
#endif
    std::error_code ec;
    const fs::path exeDir = fs::weakly_canonical(fs::absolute(fs::path(argv0), ec), ec).parent_path();
    for (const fs::path& dir : {exeDir / "web", exeDir.parent_path() / "web"}) {  // single- or multi-config builds
        if (hasIndex(dir)) return dir.string();
    }
    return "";
}

// Picks where the visitor and solve counts are kept.
std::unique_ptr<cube::server::CounterStore> makeCounterStore(const std::string& dataFile) {
    const char* url = std::getenv("UPSTASH_REDIS_REST_URL");
    const char* token = std::getenv("UPSTASH_REDIS_REST_TOKEN");
    if (url && *url) {
        if (!token || !*token) {
            std::fprintf(stderr, "UPSTASH_REDIS_REST_URL is set but UPSTASH_REDIS_REST_TOKEN is not\n");
            return nullptr;
        }
        try {
            return std::make_unique<cube::server::UpstashStore>(url, token);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Cannot use Upstash: %s\n", e.what());
            return nullptr;
        }
    }
    if (!dataFile.empty()) return std::make_unique<cube::server::FileStore>(dataFile);
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    // Block SIGTERM and SIGINT in every thread; one thread waits for them below.
    sigset_t stopSignals;
    sigemptyset(&stopSignals);
    sigaddset(&stopSignals, SIGTERM);
    sigaddset(&stopSignals, SIGINT);
    pthread_sigmask(SIG_BLOCK, &stopSignals, nullptr);
#endif

    const char* envPort = std::getenv("PORT");
    const char* envDataFile = std::getenv("CUBESOLVER_DATA_FILE");
    int port = envPort ? std::atoi(envPort) : 8080;
    std::string host = "127.0.0.1";
    std::string webDirOverride;
    std::string dataFile = envDataFile ? envDataFile : "";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--port" || arg == "--host" || arg == "--web-dir" || arg == "--data-file") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--port") port = std::atoi(value.c_str());
            if (arg == "--host") host = value;
            if (arg == "--web-dir") webDirOverride = value;
            if (arg == "--data-file") dataFile = value;
        } else if (arg == "-h" || arg == "--help") {
            std::printf(
                "Usage: cubesolver_server [--port 8080] [--host 127.0.0.1] [--web-dir path] [--data-file path]\n"
                "Environment: PORT, CUBESOLVER_DATA_FILE, UPSTASH_REDIS_REST_URL + UPSTASH_REDIS_REST_TOKEN\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s (try --help)\n", arg.c_str());
            return 2;
        }
    }
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "--port must be between 1 and 65535\n");
        return 2;
    }

    std::printf("Building solver tables... ");
    std::fflush(stdout);
    std::printf("done in %.0f ms\n", cube::tables().buildMilliseconds);

    const std::string webDir = findWebDir(argv[0], webDirOverride);
    if (webDir.empty()) {
        std::printf("Web demo files not found; only the /api endpoints are available.\n");
    } else {
        std::printf("Serving web demo from %s\n", webDir.c_str());
    }

    httplib::Server server;
    auto store = makeCounterStore(dataFile);
    if (store) {
        std::printf("Saving visitor and solve counts to %s\n", store->describe().c_str());
    } else {
        std::printf("Visitor and solve counts are kept in memory only (see --help to keep them).\n");
    }
    cube::server::Service service(2048, 4, 5000, std::move(store));
    cube::server::configureServer(server);
    cube::server::registerRoutes(server, service, webDir);

    if (!server.bind_to_port(host, port)) {
        std::fprintf(stderr, "Could not use %s:%d. Is another server running on that port? Try --port %d\n",
                     host.c_str(), port, port + 1);
        return 1;
    }
#ifndef _WIN32
    std::thread([&server, stopSignals] {
        int sig = 0;
        sigwait(&stopSignals, &sig);
        std::printf("\nStopping...\n");
        std::fflush(stdout);
        server.stop();
    }).detach();
#endif

    std::printf("\nOpen http://localhost:%d in your browser (Ctrl+C to stop)\n", port);
    std::fflush(stdout);
    const bool ok = server.listen_after_bind();
    return ok ? 0 : 1;  // ~Service saves the last counts
}
