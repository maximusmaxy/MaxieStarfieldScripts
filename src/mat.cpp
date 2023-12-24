#include "mat.h"

#include "util.h"
#include <nlohmann/json.hpp>

bool ExportMaterial(const std::string& inPath, const std::string& outPath, const cdb::Manager& manager) {
    auto matId = manager.GetMatId(inPath);
    if (matId.Value && manager.GetComponents(matId).size()) {
        try {
            nlohmann::json matJson;
            manager.CreateMaterialJson(matJson, matId, manager.idToPath);
            if (CreateDirectories(outPath)) {
                std::ofstream out(outPath, std::ios::out | std::ios::binary);
                if (!out.fail()) {
                    out << matJson.dump(2);
                    Log("{} Saved", inPath);
                    return true;
                }
                else {
                    Log("Failed to write json file {}", inPath);
                }
            }
            else {
                Log("Failed to create directories for path {}", inPath);
            }
        }
        catch (const std::exception& e) {
            Log("Error saving json for path {}\n{}", inPath, e.what());
        }
    }
    else {
        Log("Could not find material resource id for path {}", inPath);
    }

    return false;
}

