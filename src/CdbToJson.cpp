#include <iostream>
#include <array>

#include <nlohmann/json.hpp>

#include "types.h"
#include "cdb.h"
#include "crc.h"
#include "nif.h"
#include "esp.h"
#include "bs.h"
#include "bsa.h"
#include "util.h"

const std::array rootMaterialPaths{
    "materials\\layered\\root\\materials.mat",
    "materials\\layered\\root\\blenders.mat",
    "materials\\layered\\root\\texturesets.mat",
    "materials\\layered\\root\\uvstreams.mat",
    "materials\\layered\\root\\layers.mat",
    "materials\\layered\\root\\layeredmaterials.mat",
};

bool GetMaterialPathsFromEsp(PathSet& paths, const std::string& src) {
    using namespace esp;

    Reader in(src);
    if (in.Fail())
        return false;

    in.ReadHeader();

    const std::vector<uint32_t> formTypes{
        Sig("MTPT")
    };

    //in.ForEachGroup(formTypes, [&](const Group& group) {
    //    switch (group.value) {
    //    case Sig("MTPT"):
    //    {
    //        in.ForEachRecord(group, [&](const esp::Record& record) {
    //            Element element;
    //            auto remaining = record.size;
    //            remaining = in.SeekToElement(remaining, Sig("REFL"), element);
    //            if (remaining) {
    //                auto component = cdb::Reader(*in.Stream());
    //                component.ReadHeader();
    //                while (!component.End()) {
    //                    if (component.SeekToType("BGSMaterialPathForm")) {
    //                        paths.emplace(component.ReadFixed());
    //                    }
    //                }
    //                remaining -= element.len;
    //                in.Skip(remaining);
    //            }
    //        });
    //        break;
    //    }
    //    }
    //});

    return true;
}

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

    //{
    //    InsensitiveSet pathSet;
    //    if (!GetMaterialPathsFromNifsRecursive(pathSet, "E:\\Export\\meshes")) {
    //        Log("Failed to get material paths from nifs");
    //        return false;
    //    }

    //    if (!GetMaterialPathsFromEsp(pathSet, "E:\\SteamLibrary\\steamapps\\common\\Starfield\\Data\\Starfield.esm")) {
    //        Log("Failed to get material paths from esp");
    //        return false;
    //    }

    //    for (auto& path : pathSet) {
    //        result.emplace_back(path);
    //    }

    //    return true;
    //}

    //std::ofstream out(txtPath);
    //if (out.fail()) {
    //    Log("Failed to save material path cache: {}", txtPath);
    //    return result;
    //}

    //for (auto& path : result) {
    //    out << path << "\n";
    //}

    //return result;
}

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

        //header.database.
        auto& compiledDb = json["CompiledDB"];
        compiledDb["BuildVersion"] = header.database.BuildVersion;
        auto& hashMap = compiledDb["HashMap"];
        for (auto& [key, value] : header.database.HashMap) {
            hashMap[GetFormatedResourceId(key)] = std::to_string(value);
        }

        //auto& index = json["FileIndex"];
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

        //nlohmann::json keyjson = nlohmann::json::array();
        //for (auto& key : unknownKeySet) {
        //    auto c = (const char*)&key;
        //    bool isValidExtension = true;
        //    for (int i = 0; i < 4; ++i) {
        //        if (!((c[i] >= 'a' && c[i] <= 'z') ||
        //            (c[i] >= 'A' && c[i] <= 'Z'))) {
        //            isValidExtension = false;
        //        }
        //    }
        //    if (isValidExtension) {
        //        keyjson.emplace_back() = std::string_view(c, 4);
        //    }
        //}
        //const char* dumpPath = "unknownkeys.json";
        //std::ofstream out(dumpPath, std::ios::out | std::ios::binary);
        //if (out.fail()) {
        //    Log("Failed to dump keys to json {}", dumpPath);
        //    return false;
        //}
        //out << keyjson.dump(2);

        //auto& components = index["Components"];
        //for (auto& component : header.fileIndex.Components) {
        //    auto& componentValue = components.emplace_back();
        //    componentValue["ID"] = component.ObjectID.Value;
        //    componentValue["Index"] = component.Index;
        //    componentValue["Type"] = typeMap.at(component.Type);
        //}

        //auto& edges = index["Edges"];
        auto& edges = json["Edges"];
        for (auto& edge : header.fileIndex.Edges) {
            auto& edgeValue = edges.emplace_back();
            edgeValue["SrcID"] = edge.SourceID.Value;
            edgeValue["TgtID"] = edge.TargetID.Value;
            edgeValue["Index"] = edge.Index;
            edgeValue["Type"] = typeMap.at(edge.Type);
        }

        //auto& componentData = json["Components"];
        //for (uint32_t i = 0; i < header.fileIndex.Components.size(); ++i) {
        //    auto& componentValue = componentData.emplace_back();
        //    auto& component = header.fileIndex.Components.at(i);
        //    componentValue = header.componentJsons.at(i);
        //    componentValue["Idx"] = i;
        //    componentValue["ObjID"] = component.ObjectID.Value;
        //    const auto& obj = header.GetObject(component.ObjectID);
        //    if (obj)
        //        componentValue["PrtID"] = obj.Parent.Value;
        //}
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

bool DumpClasses(cdb::Manager& header) {
    nlohmann::json json;
    for (auto& [name, value] : header.classJsons) {
        json[name] = value;
    }

    const char* dumpPath = "Classes.json";
    std::ofstream out(dumpPath);
    if (out.fail()) {
        Log("Failed to dump cdb to json {}", dumpPath);
        return false;
    }

    out << json.dump(2);
    return true;
}

struct PathInfo {
    std::vector<std::string> paths;
    std::vector<std::string> materials;
    std::string cdb;
    std::string exe;
};

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

    //DumpClasses(header);
    //std::set<uint32_t> ext;
    //for (auto& obj : header.fileIndex.Objects) {
    //    if (obj.PersistentID.ext != 'tam') {
    //        ext.emplace(obj.PersistentID.ext);
    //    }
    //}

    //for (auto& ex : ext) {
    //    Log("{:08X}", ex);
    //}

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
            //try {
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
            //}
            //catch (const std::exception& e) {
            //    Log("Error saving json for path {}\n{}", path, e.what());
            //}
        }
        else {
            Log("Could not find material resource id for path {}", path);
        }
    }

    Log("Complete!");
    return true;
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
    if (argc == 1) {
        LogHelp(argv[0]);
        auto _ = getchar();
        return 0;
    }

    PathInfo paths;
    paths.exe = argv[0];
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
        return -1;
    }

    {
        PathSet pathset;
        for (auto& path : rootMaterialPaths) {
            paths.paths.emplace_back(path);
        }
    }

    bool noWait = false;

    {
        PathSet pathSet;
        for (int i = 1; i < argc; ++i) {
            std::string path(argv[i]);
            if (path == "-nowait" || path == "-nw") {
                noWait = true;
            }
            if (path == "-h" || path == "-help" || path == "h" || path == "help") {
                LogHelp(argv[0]);
                if (!noWait)
                    auto _ = getchar();
                return 0;
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
                Log("Search for .mat paths in {}", path);
                if (!GetMaterialPathsFromBsa(pathSet, path)) {
                    Log("Failed to open .nif file: {}", path);
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
    }

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
        LogHelp(argv[0]);
        if (!noWait)
            auto _ = getchar();
        return -1;
    }

    //DumpDb(paths.cdb, paths.materials);
    //return 0;

    DumpMats(paths);

    if (!noWait)
        auto _ = getchar();

    return 0;
}