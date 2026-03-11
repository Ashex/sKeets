#include "paths.h"

#include <cstdlib>
#include <mutex>
#include <string>

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace {

struct path_state_t {
    std::string data_dir;
    std::string config_path;
    std::string cache_dir;
};

path_state_t g_paths;
std::once_flag g_paths_once;

void init_paths() {
    const char *env_dir = std::getenv("SKEETS_DATA_DIR");
    g_paths.data_dir = (env_dir && *env_dir) ? env_dir : "/mnt/onboard/.adds/sKeets";

    while (g_paths.data_dir.size() > 1 && g_paths.data_dir.back() == '/')
        g_paths.data_dir.pop_back();

    g_paths.config_path = g_paths.data_dir + "/config.ini";
    g_paths.cache_dir = g_paths.data_dir + "/cache";
}

const path_state_t &paths() {
    std::call_once(g_paths_once, init_paths);
    return g_paths;
}

bool mkdir_if_missing(const std::string &path) {
    if (path.empty()) return false;
    if (mkdir(path.c_str(), 0755) == 0) return true;
    if (errno != EEXIST) return false;

    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool mkdirs(const std::string &path) {
    if (path.empty()) return false;

    std::string current;
    if (path[0] == '/') current = "/";

    size_t pos = (path[0] == '/') ? 1 : 0;
    while (pos <= path.size()) {
        const size_t slash = path.find('/', pos);
        const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (!part.empty()) {
            if (!current.empty() && current.back() != '/') current.push_back('/');
            current += part;
            if (!mkdir_if_missing(current)) return false;
        }
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }

    return true;
}

} // namespace

const char *skeets_data_dir() {
    return paths().data_dir.c_str();
}

const char *skeets_config_path() {
    return paths().config_path.c_str();
}

const char *skeets_cache_dir() {
    return paths().cache_dir.c_str();
}

int skeets_ensure_data_dirs() {
    const path_state_t &state = paths();
    if (!mkdirs(state.data_dir)) return -1;
    if (!mkdirs(state.cache_dir)) return -1;
    return 0;
}