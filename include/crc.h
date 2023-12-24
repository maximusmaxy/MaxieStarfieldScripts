#pragma once

#include <string>

#include "bs.h"

uint32_t GetCrc(const std::string_view sv);
BSResource::ID GetResourceIdFromPath(const std::string& path);
std::string GetFormatedResourceId(const BSResource::ID& id);