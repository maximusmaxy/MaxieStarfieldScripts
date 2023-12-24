#pragma once

#include <string>

#include "cdb.h"

constexpr std::array rootMaterialPaths{
    "materials\\layered\\root\\materials.mat",
    "materials\\layered\\root\\blenders.mat",
    "materials\\layered\\root\\texturesets.mat",
    "materials\\layered\\root\\uvstreams.mat",
    "materials\\layered\\root\\layers.mat",
    "materials\\layered\\root\\layeredmaterials.mat",
};

bool ExportMaterial(const std::string& inPath, const std::string& outPath, const cdb::Manager& manager);
