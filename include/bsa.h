#pragma once

#include <vector>
#include <string>

#include "types.h"

bool GetMaterialDatabase(const std::string& path, std::vector<char>& bytes);
bool GetMaterialPathsFromBsa(PathSet& pathSet, const std::string& path);