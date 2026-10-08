// Web server: JSON API + the 3D web demo.
//
//   cubesolver_server [--port 8080] [--host 127.0.0.1] [--web-dir path] [--data-file path]
//
// Hosting services pass the port in the PORT environment variable; it is used
// when --port is not given. CUBESOLVER_DATA_FILE works the same way for
// --data-file, the file that keeps the visitor and solve counts.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

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

}  // namespace

int main(int argc, char** argv) {
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
                "Usage: cubesolver_server [--port 8080] [--host 127.0.0.1] [--web-dir path] [--data-file path]\n");
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
    cube::server::Service service(2048, 4, 5000, dataFile);
    if (dataFile.empty()) {
        std::printf("Visitor and solve counts are kept in memory only (use --data-file to save them).\n");
    } else {
        std::printf("Saving visitor and solve counts to %s\n", dataFile.c_str());
    }
    cube::server::configureServer(server);
    cube::server::registerRoutes(server, service, webDir);

    if (!server.bind_to_port(host, port)) {
        std::fprintf(stderr, "Could not use %s:%d. Is another server running on that port? Try --port %d\n",
                     host.c_str(), port, port + 1);
        return 1;
    }
    std::printf("\nOpen http://localhost:%d in your browser (Ctrl+C to stop)\n", port);
    std::fflush(stdout);
    return server.listen_after_bind() ? 0 : 1;
}
