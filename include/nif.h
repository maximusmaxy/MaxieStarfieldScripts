#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

#include "types.h"

bool GetMaterialPathsFromNifPath(PathSet& result, const std::filesystem::path& path);
bool GetMaterialPathsFromNifStream(PathSet& result, std::istream& stream);
bool GetMaterialPathsFromNifsRecursive(PathSet& result, const std::filesystem::path& folder);