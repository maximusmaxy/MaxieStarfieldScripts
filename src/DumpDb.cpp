#include <iostream>

#include <nlohmann/json.hpp>

#include "types.h"
#include "cdb.h"
#include "util.h"
#include "paths.h"

bool DumpDb(const std::string& dbPath, const std::vector<std::string>& materialPaths) {
    cdb::Manager header;
    try {
        std::ifstream stream(dbPath, std::ios::in | std::ios::binary);
        if (stream.fail()) {
            Log("Failed to open material database {}", dbPath);
            return false;
        }
        cdb::Reader in(stream);
        in.ReadHeader(header);
        in.ReadAllComponents(header);
    }
    catch (const std::exception& e) {
        Log("{}", e.what());
        return false;
    }

    nlohmann::json json;
    {
        std::unordered_map<BSResource::ID, const std::string&> resourceToPath;
        for (const auto& path : materialPaths) {
            resourceToPath.emplace(GetResourceIdFromPath(path), path);
        }
        std::map<uint16_t, std::string> typeMap;
        for (auto& [key, type] : header.fileIndex.ComponentTypes) {
            typeMap[key] = type.Class;
        }

        auto& compiledDb = json["CompiledDB"];
        compiledDb["BuildVersion"] = header.database.BuildVersion;
        auto& hashMap = compiledDb["HashMap"];
        for (auto& [key, value] : header.database.HashMap) {
            hashMap[GetFormatedResourceId(key)] = std::to_string(value);
        }

        auto& types = json["Types"];
        for (auto& type : header.fileIndex.ComponentTypes) {
            auto& typeValue = types.emplace_back();
            typeValue["Id"] = type.first;
            typeValue["Class"] = type.second.Class;
            typeValue["Version"] = type.second.Version;
            typeValue["Empty"] = type.second.IsEmpty;
        }

        //std::unordered_set<uint32_t> unknownKeySet;

        //auto& objects = index["Objects"];
        auto& objects = json["Objects"];
        for (auto& object : header.fileIndex.Objects) {
            auto& objectValue = objects.emplace_back();
            objectValue["ResourceID"] = GetFormatedResourceId(object.PersistentID);
            objectValue["DbID"] = object.DBID.Value;
            //objectValue["Parents"] = object.Parent.Value;
            //objectValue["HasData"] = object.HasData;
            if (object.HasData && object.Parent.Value) {
                const auto parentList = header.GetParentList(object.DBID);
                auto& parentsValue = objectValue["Parents"];
                for (auto parentIt = parentList.rbegin(); parentIt != parentList.rend() - 1; parentIt++) {
                    parentsValue.emplace_back(parentIt->Value);
                }
            }
            auto it = resourceToPath.find(object.PersistentID);
            if (it != resourceToPath.end())
                objectValue["Path"] = it->second;
            else {
                if (object.PersistentID.ext == 'tam') {
                    objectValue["Path"] = "<unknown>.mat";
                }
                //else {
                //    unknownKeySet.emplace(key.ext);
                //}
            }
            auto& components = header.GetComponents(object.DBID);
            if (components.size()) {
                auto& componentsValue = objectValue["Components"];
                for (auto& ref : components) {
                    auto& componentValue = componentsValue.emplace_back();
                    auto& component = header.fileIndex.Components.at(ref.idx);
                    componentValue = header.componentJsons.at(ref.idx);
                    componentValue["Idx"] = ref.idx;
                }
            }
        }

        auto& edges = json["Edges"];
        for (auto& edge : header.fileIndex.Edges) {
            auto& edgeValue = edges.emplace_back();
            edgeValue["SrcID"] = edge.SourceID.Value;
            edgeValue["TgtID"] = edge.TargetID.Value;
            edgeValue["Index"] = edge.Index;
            edgeValue["Type"] = typeMap.at(edge.Type);
        }
    }

    const char* dumpPath = "Dump.json";
    std::ofstream out(dumpPath, std::ios::out | std::ios::binary);
    if (out.fail()) {
        Log("Failed to dump cdb to json {}", dumpPath);
        return false;
    }

    out << json.dump(2);
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

    DumpDb(paths.cdb, paths.materials);

    if (!paths.noWait)
        auto _ = getchar();

    return 0;
}