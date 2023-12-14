#include "util.h"

#include <filesystem>
#include <vector>
#include <string>

#include "cdb.h"
#include "util.h"
#include "crc.h"
#include "bs.h"

bool GetMaterialPaths(const std::string& materialsFolder, std::vector<std::string>& paths) {
    std::error_code ec;
    auto dirIt = std::filesystem::recursive_directory_iterator(materialsFolder, ec);
    if (ec) {
        Log("Error opening folder iterator {}", ec.message());
        return false;
    }

    for (auto& entry : dirIt) {
        if (HasExtension(entry.path(), L".mat")) {
            std::string materialPath = entry.path().string();
            SanitizePrefixedPath(materialPath, "materials");
            paths.emplace_back(materialPath);
        }
    }
    
    return true;
}

struct PathInfo {
    std::string materials;
    std::string cdbIn;
    std::string cdbOut;
    bool forceUpdate;
    bool test;
};

bool RecompileDatabase(const PathInfo& paths) {
    using namespace cdb;

    std::vector<std::string> materialPaths;
    if (!GetMaterialPaths(paths.materials, materialPaths))
        return false;

    if (!paths.forceUpdate && materialPaths.empty()) {
        Log("No .mat files found in Materials folder. Aborting");
        return false;
    }

    if (!CreateDirectories(paths.cdbOut)) {
        Log("Failed to create directory for output {}", paths.cdbOut);
        return false;
    }

    Manager header;
    {
        std::ifstream stream(paths.cdbIn, std::ios::in | std::ios::binary);
        if (stream.fail()) {
            Log("Failed to open cdb file {}", paths.cdbIn);
            return false;
        }

        Log("Reading material database {}", paths.cdbIn);
        Reader in(stream);

        if (!in.ReadHeader(header))
            return false;

        if (!in.ReadAllComponents(header)) {
            Log("Error reading material component diffs {}", paths.cdbIn);
            return false;
        }
    }

    TestStruct tester;
    tester.hash = 56916906640545u;
    tester.resourceId = GetResourceIdFromPath("materials\\actors\\human\\faces\\female_default.mat");
    auto testerIt = header.resourceToDb.find(tester.resourceId);
    tester.matId = testerIt != header.resourceToDb.end() ? testerIt->second : BSComponentDB2::ID{ 0 };
    header.GetComponentIndexesForMaterial(tester.matId, tester);

    bool anyUpdated = false;
    std::unordered_map<uint32_t, nlohmann::json> updates;
    std::vector<Writer::CreateInfo> creates;
    Log("Checking for updated materials");

    for (auto& matPath : materialPaths) {
        nlohmann::json matJson;

        {
            const auto jsonPath = std::filesystem::path(paths.materials).append(matPath.begin() + sizeof("Materials"), matPath.end());
            std::ifstream jsonStream(jsonPath, std::ios::in | std::ios::binary);
            if (jsonStream.fail()) {
                Log("Failed to open .mat file {}", jsonPath.string());
                continue;
            }
            jsonStream >> matJson;
        }

        auto matResourceId = GetResourceIdFromPath(matPath);
        auto pathIt = header.resourceToDb.find(matResourceId);
        auto matDbid = pathIt != header.resourceToDb.end() ? pathIt->second : BSComponentDB2::ID{ 0 };

        //Existing
        if (matDbid.Value) {
            nlohmann::json dbJson;
            //TODO fix idToPath map
            header.CreateMaterialJson(dbJson, matDbid, {});

            const bool isUpdated = header.CompareJsons(matJson, dbJson);
            if (isUpdated) {
                anyUpdated = true;
                Log("Existing material updated {}", matPath);
            }
            //else {
            //    Log("Existing material not updated {}", matPath);
            //}
        }
        //New
        else {
            header.UpdateDatabaseIds(matJson, matPath);
            //TODO GET HASH
            //creates.emplace_back(std::move(matJson), matResourceId, GetHashFromBSResourceId(matResourceId));
            creates.emplace_back(std::move(matJson), matResourceId, 56916906640545u);
            anyUpdated = true;
            Log("New material added {}", matPath);
        }
    }

    if (!paths.forceUpdate && !anyUpdated) {
        Log("No new or updated materials found.");
        return false;
    }

    {
        std::ofstream outStream(paths.cdbOut, std::ios::out | std::ios::binary);
        if (outStream.fail()) {
            Log("Failed to create .cdb file {}", paths.cdbOut);
            return false;
        }

        std::ifstream stream(paths.cdbIn, std::ios::in | std::ios::binary);
        if (stream.fail()) {
            Log("Failed to open cdb file again {}", paths.cdbIn);
            return false;
        }

        //Log("Reading material database {}", cdbIn);
        Reader in(stream);
        in.ReadHeader();
        in.SkipNextObject();
        in.SkipNextObject();

        Log("Recompiling database");

        //uint32_t chunkSize = in.ChunkSize();
        //for (auto& createInfo : creates) {
        //    auto& objects = createInfo.json["Objects"];
        //    for (auto& object : objects) {
        //        auto& components = object["Components"];
        //        for (auto& component : components) {
        //            //Objt
        //            chunkSize++;
        //            in.GetJsonChunkCount(component, chunkSize);
        //        }
        //    }
        //}
        uint32_t chunkSize = uint32_t(in.HeaderChunkSize());
        for (auto& componentIdx : tester.componentSet) {
            const auto& component = header.componentJsons.at(componentIdx);
            chunkSize++;
            in.GetJsonChunkCount(component, chunkSize);
        }

        Writer::Header outHeader{
            in.Version(),
            chunkSize,
            in.StringTable(),
            in.Classes(),
        };

        Writer out(outStream, outHeader);
        out.WriteHeader();
        if (!paths.test) {
            out.WriteDatabase(header, creates);
            for (uint32_t i = 0; i < header.fileIndex.Components.size(); ++i) {
                //auto updateIt = updates.find(i);
                //if (updateIt != updates.end()) {
                //    //out.WriteComponentJson();
                //}
                //else {
                //    out.WriteChunk(in);
                //}
                out.WriteChunk(in);
            }
            for (auto& createInfo : creates) {
                auto& objects = createInfo.json["Objects"];
                for (auto& object : objects) {
                    auto& components = object["Components"];
                    for (auto& component : components) {
                        out.WriteComponentJson(component);
                    }
                }
            }
        }
        else {
            out.WriteTestDatabase(header, tester);
            for (uint32_t i = 0; i < header.fileIndex.Components.size(); ++i) {
                if (tester.componentSet.contains(i))
                    out.WriteChunk(in);
                else
                    in.SkipNextObject();
            }

            nlohmann::json testdump;
            for (auto& componentIdx : tester.componentSet) {
                testdump.emplace_back() = header.componentJsons.at(componentIdx);
            }

            std::ofstream teststream("testdump.json", std::ios::out | std::ios::binary);
            if (teststream.fail())
                Log("lol");
            teststream << testdump.dump(2);
        }
    }

    Log("Recompiled database written to {}", paths.cdbOut);
    return true;
}

int main(int argc, char** argv) {
    const auto materialsFolder = std::filesystem::path(argv[0]).remove_filename().append("Materials");

    const PathInfo paths{
        .materials = materialsFolder.string(),
        .cdbIn = std::filesystem::path(materialsFolder).append("materialsbeta_original.cdb").string(),
        //.cdbOut = std::filesystem::path(materialsFolder).append("materialsbeta.cdb").string(),
        .cdbOut = std::filesystem::path(materialsFolder).append("materialsbeta_test.cdb").string(),
        .forceUpdate = true,
        .test = true,
    };

    if (!RecompileDatabase(paths))
        return -1;
    return 0;
}