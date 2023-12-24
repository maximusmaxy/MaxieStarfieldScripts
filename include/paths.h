#pragma once

#include <vector>
#include <string>
#include <functional>
#include <filesystem>

struct PathInfo {
    const std::function<void(const char*)>& LogHelp;
    std::vector<std::string> paths;
    std::vector<std::string> materials;
    std::string cdb;
    std::string exe;
    bool noWait = false;
};

bool GetPathInfo(PathInfo& paths, int argc, char** argv);
bool GetAllPaths(PathInfo& paths, int argc, char** argv);
std::filesystem::path GetRegistryPath(const wchar_t* subkey, const wchar_t* value);
