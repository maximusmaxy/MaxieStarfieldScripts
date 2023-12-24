#include <iostream>
#include <spanstream>

#include <nlohmann/json.hpp>

#include "types.h"
#include "bsa.h"
#include "cdb.h"
#include "util.h"
#include "paths.h"

bool DumpMats(const PathInfo& paths) {
    using namespace cdb;

    std::ifstream fstream;
    std::vector<char> ba2Buffer;
    std::ispanstream ba2Stream(std::span{ ba2Buffer });
    
    const auto& GetReader = [&]() -> Reader {
        if (HasExtension(paths.cdb, ".cdb")) {
            fstream.open(paths.cdb, std::ios::in | std::ios::binary);
            if (fstream.fail())
                Log("Failed to open cdb file {}", paths.cdb);
            return Reader(fstream);
        }
        else if (HasExtension(paths.cdb, ".ba2")) {
            bool result = GetMaterialDatabase(paths.cdb, ba2Buffer);
            ba2Stream = std::ispanstream(std::span{ ba2Buffer });
            if (!result || ba2Buffer.empty())
                ba2Stream.setstate(std::ios::failbit);
            return Reader(ba2Stream);
        }
        Log("Unknown extension for cdb file {}", paths.cdb);
        ba2Stream.setstate(std::ios::failbit);
        return Reader(ba2Stream);
    };
    
    Reader in = GetReader();
    if (in.Stream().fail())
        return false;

    Log("Reading material database {}", paths.cdb);
    Manager header;
    if (!in.ReadHeader(header))
        return false;

    if (!in.ReadAllComponents(header)) {
        Log("Error reading material component diffs {}", paths.cdb);
        return false;
    }

    std::unordered_map<uint32_t, std::string> idToPath;
    for (auto& path : paths.paths) {
        auto resourceId = GetResourceIdFromPath(path);
        auto idIt = header.resourceToDb.find(resourceId);
        if (idIt != header.resourceToDb.end()) {
            idToPath.emplace(idIt->second.Value, path);
        }
    }
 
    Log("Writing materials");
    for (auto& path : paths.materials) {
        auto matId = header.GetMatId(path);
        if (matId.Value && header.GetComponents(matId).size()) {
            try {
                nlohmann::json matJson;
                header.CreateMaterialJson(matJson, matId, idToPath);
                const auto outPath = std::filesystem::path(paths.exe).remove_filename().append(path).string();
                if (CreateDirectories(outPath)) {
                    std::ofstream out(outPath, std::ios::out | std::ios::binary);
                    if (!out.fail()) {
                        out << matJson.dump(2);
                        Log("{} Saved", path);
                    }
                    else {
                        Log("Failed to write json file {}", path);
                    }
                }
                else {
                    Log("Failed to create directories for path {}", path);
                }
            }
            catch (const std::exception& e) {
                Log("Error saving json for path {}\n{}", path, e.what());
            }
        }
        else {
            Log("Could not find material resource id for path {}", path);
        }
    }

    Log("Complete!");
    return true;
}

void LogHelp(const char* exePath) {
    const auto exeName = std::filesystem::path(exePath).filename().string();

    std::cout << " --- " << exeName << " ---\n"
        << "\n"
        << "You can drag and drop files onto " << exeName << " instead of using the command line\n"
        << "\n"
        << "Usage: \n"
        << "  " << exeName << " <path>.mat - Dumps the .mat for the specified path\n"
        << "  " << exeName << " <path>.txt - Dumps all .mat files specified in a .txt file\n"
        << "  " << exeName << " <path>.nif - Searches nif file for .mat files and dumps all found\n"
        << "  " << exeName << " <folder> - Searches folder recursively for nif files and dumps all .mat files found\n"
        << "  " << exeName << " <path>.cdb - Manually specify the cdb path\n"
        << "\n"
        << "Options: \n"
        << "  -help -h     Shows this help message\n"
        << "  -nowait -nw  Disables the wait for user input on completion\n";
}

int main(int argc, char** argv) {    
    PathInfo paths{ LogHelp };
    if (!GetPathInfo(paths, argc, argv))
        return -1;

    DumpMats(paths);

    if (!paths.noWait)
        auto _ = getchar();

    return 0;
}