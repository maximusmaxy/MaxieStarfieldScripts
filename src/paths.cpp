#include "paths.h"

#include <fstream>
#include <nlohmann/json.hpp>

#include "types.h"
#include "util.h"
#include "nif.h"
#include "bsa.h"
#include "esp.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "windows.h"

bool GetMaterialPathsFromTxt(PathSet& result, const std::string& txtPath) {
    if (!std::filesystem::exists(txtPath))
        return false;

    std::ifstream in(txtPath);
    if (in.fail()) {
        Log("Failed to open material paths {}", txtPath);
        return false;
    }
    std::string line;
    while (std::getline(in, line, '\n')) {
        if (line.back() == '\r')
            line.pop_back();
        if (HasExtension(line, ".mat")) {
            SanitizePrefixedPath(line, "material");
            result.emplace(line);
        }
    }

    return true;
}

bool GetMaterialPathsFromEsp(PathSet& result, const std::string& espPath) {
    using namespace esp;

    Reader in(espPath);
    if (in.Fail()) {
        Log("Failed to open esp path {}", espPath);
        return false;
    }

    in.ReadHeader();

    //std::unordered_set<uint32_t> matSigs;
    //uint32_t currentGroup = 0;

    const auto& FindPathsInStream = [&](std::istream& in, const uint32_t size) {
        char buffer[MAX_PATH] = {};
        uint32_t bufIdx = 0;
        uint32_t checkIdx = 0;
        uint32_t streamCount = 0;
        while (streamCount < size) {
            buffer[bufIdx] = in.get();
            constexpr char matExt[] = ".mat";
            //constexpr char matExtUpper[] = ".MAT";
            //if (buffer[bufIdx] == matExt[checkIdx] || buffer[bufIdx] == matExtUpper[checkIdx]) {
            if (buffer[bufIdx] == matExt[checkIdx]) {
                if (++checkIdx == sizeof(matExt)) {
                    uint32_t pathSize = sizeof(matExt);
                    uint32_t pathIdx = bufIdx + 1;
                    if (pathIdx < pathSize)
                        pathIdx += MAX_PATH;
                    pathIdx -= pathSize;
                    uint8_t checkSize[2];
                    while (pathSize < MAX_PATH) {
                        if (pathIdx == 0)
                            pathIdx = MAX_PATH;
                        checkSize[1] = buffer[--pathIdx];
                        if (pathIdx == 0)
                            pathIdx = MAX_PATH;
                        checkSize[0] = buffer[--pathIdx];
                        if (*reinterpret_cast<uint16_t*>(checkSize) == pathSize) {
                            pathIdx += 2;
                            if (pathIdx >= MAX_PATH)
                                pathIdx -= MAX_PATH;
                            if (pathIdx + pathSize > MAX_PATH) {
                                std::string path(buffer + pathIdx, MAX_PATH - pathIdx);
                                path.append(buffer, pathIdx + pathSize - MAX_PATH - 1);
                                SanitizePrefixedPath(path, "Materials");
                                result.emplace(path);
                                //matSigs.emplace(currentGroup);
                            }
                            else {
                                std::string path(buffer + pathIdx, pathSize - 1);
                                SanitizePrefixedPath(path, "Materials");
                                result.emplace(path);
                                //matSigs.emplace(currentGroup);
                            }
                            break;
                        }
                        ++pathIdx;
                        ++pathSize;
                    }
                    checkIdx = 0;
                }
            }
            else {
                checkIdx = 0;
            }
            if (++bufIdx >= MAX_PATH)
                bufIdx = 0;
            ++streamCount;
        }
    };

    const std::vector<uint32_t> matGroups{
        Sig("LTEX"),
        Sig("ACTI"),
        Sig("MSTT"),
        Sig("DOOR"),
        Sig("STAT"),
        Sig("WRLD"),
        Sig("LMSW"),
        Sig("BIOM"),
        Sig("EFSQ"),
        Sig("MTPT"),
    };

    in.ForEachGroup(matGroups, [&](const Group& group) {
        FindPathsInStream(in.Stream(), group.size - 0x18);
    });

    //in.ForEachGroup([&](const Group& group) {
    //    currentGroup = group.value;
    //    in.ForEachRecord(group, [&](const Record& record) {
    //        FindPathsInStream(in.Stream(), record.size);
    //    });
    //});

    //Log("Groups with materials");
    //for (auto& sig : matSigs) {
    //    Log("{}", std::string_view((char*)&sig, 4));
    //}
    return true;
}

std::filesystem::path GetRegistryPath(const wchar_t* subkey, const wchar_t* value) {
    DWORD pathSize = MAX_PATH;
    std::wstring pathBuffer(pathSize, 0);

    auto status = RegGetValueW(
        HKEY_LOCAL_MACHINE,
        subkey,
        value,
        RRF_RT_REG_SZ,
        nullptr,
        (void*)pathBuffer.data(),
        &pathSize
    );

    return std::filesystem::path(status == ERROR_SUCCESS ? pathBuffer.c_str() : L"");
}

nlohmann::json ReadSettings(const std::string& path) {
    nlohmann::json result;
    if (std::filesystem::exists(path)) {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (in.fail())
            return result;
        in >> result;
        return result;
    }
    else {
        const wchar_t* sfRegSubkey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 1716740";
        auto sfPath = GetRegistryPath(sfRegSubkey, L"InstallLocation");
        if (!sfPath.empty()) {
            result["cdb_path"] = std::filesystem::path(sfPath).append("Data\\Starfield - Materials.ba2");
            result["ba2_path"] = std::filesystem::path(sfPath).append("Data\\Materials\\materialsbeta.cdb");
        }
        else {
            result["cdb_path"] = "";
            result["ba2_path"] = "";
            Log("Failed to find starfield install location in registry");
        }
        std::ofstream out(path, std::ios::out | std::ios::binary);
        if (!out.fail())
            out << result.dump(2);
        else
            Log("Failed to create settings file {}", path);
    }
    return result;
}

bool GetAllPaths(PathInfo& paths, int argc, char** argv) {
    PathSet pathSet;
    for (int i = 1; i < argc; ++i) {
        std::string path(argv[i]);
        if (path == "-nowait" || path == "-nw") {
            paths.noWait = true;
        }
        if (path == "-h" || path == "-help" || path == "h" || path == "help") {
            paths.LogHelp(argv[0]);
            if (!paths.noWait)
                auto _ = getchar();
            return false;
        }
        else if (HasExtension(path, ".cdb")) {
            paths.cdb = path;
        }
        else if (HasExtension(path, ".txt")) {
            if (!GetMaterialPathsFromTxt(pathSet, path))
                continue;
        }
        else if (HasExtension(path, ".nif")) {
            if (!GetMaterialPathsFromNifPath(pathSet, path)) {
                Log("Failed to open .nif file: {}", path);
                continue;
            }
            //if (pathSet.empty()) {
            //    Log("Failed to find .mat paths in .nif: {}", path);
            //    continue;
            //}
            //for (auto& path : pathSet) {
            //    matPaths.emplace_back(path);
            //}
        }
        else if (HasExtension(path, ".mat")) {
            SanitizePrefixedPath(path, "material");
            pathSet.emplace(std::move(path));
        }
        else if (HasExtension(path, ".ba2")) {
            Log("Searching for .mat paths in {}", path);
            if (!GetMaterialPathsFromBsa(pathSet, path)) {
                Log("Failed to open .nif file: {}", path);
                continue;
            }
        }
        else if (HasExtension(path, ".esp") || HasExtension(path, ".esl") || HasExtension(path, ".esm")) {
            Log("Searching for .mat paths in {}", path);
            if (!GetMaterialPathsFromEsp(pathSet, path)) {
                Log("Failed to get paths from {}", path);
                continue;
            }
        }
        else if (std::filesystem::is_directory(path)) {
            Log("Searching for .mat paths in .nif files for folder: {}", path);
            if (!GetMaterialPathsFromNifsRecursive(pathSet, path)) {
                Log("Failed to get nif folder: {}", path);
                continue;
            }
            //if (pathSet.empty()) {
            //    Log("Failed to find .mat paths in .nif files from folder: {}", path);
            //    continue;
            //}
        }
    }

    for (auto& path : pathSet) {
        paths.materials.emplace_back(path);
        //Log("{}", path);
    }

    return true;
}

bool GetPathInfo(PathInfo& paths, int argc, char** argv) {
    auto rootPath = std::filesystem::path(paths.exe).remove_filename();

    const auto settingsPath = std::filesystem::path(rootPath).append("Settings.json").string();
    auto settings = ReadSettings(settingsPath);
    const auto materialsFolder = std::filesystem::path(rootPath).append("Materials");

    if (settings.contains("cdb_path") && std::filesystem::exists(settings["cdb_path"])) {
        paths.cdb = settings["cdb_path"];
    }
    else if (settings.contains("ba2_path") && std::filesystem::exists(settings["ba2_path"])) {
        paths.cdb = settings["ba2_path"];
    }
    else {
        auto cbd = std::filesystem::path(rootPath).append("materialsbeta.cdb");
        auto ba2 = std::filesystem::path(rootPath).append("Starfield - Materials.ba2");
        if (std::filesystem::exists(cbd)) {
            paths.cdb = cbd.string();
        }
        else if (std::filesystem::exists(ba2)) {
            paths.cdb = ba2.string();
        }
    }

    if (paths.cdb.empty()) {
        Log("Failed to find either \"materialsbeta.cdb\" or \"Starfield - Materials.ba2\"");
        Log("Please specify the path of either file in the \"{}\"", settingsPath);
        auto _ = getchar();
        return false;
    }

    constexpr std::array rootMaterialPaths{
        "materials\\layered\\root\\materials.mat",
        "materials\\layered\\root\\blenders.mat",
        "materials\\layered\\root\\texturesets.mat",
        "materials\\layered\\root\\uvstreams.mat",
        "materials\\layered\\root\\layers.mat",
        "materials\\layered\\root\\layeredmaterials.mat",
    };

    for (auto& path : rootMaterialPaths) {
        paths.paths.emplace_back(path);
    }

    if (!GetAllPaths(paths, argc, argv))
        return false;

    if (paths.materials.empty()) {
        std::cout << "Failed to find any .mat paths in";
        if (argc == 2) {
            std::cout << " " << argv[1] << "\n";
        }
        else {
            std::cout << ":\n";
            for (int i = 1; i < argc; ++i) {
                std::cout << "  " << argv[1] << "\n";
            }
        }
        paths.LogHelp(argv[0]);
        if (!paths.noWait)
            auto _ = getchar();
        return false;
    }

    return true;
}
