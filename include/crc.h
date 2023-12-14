#pragma once

#include <string>

#include "bs.h"

uint32_t GetCrc(const std::string_view sv);
uint64_t GetCrc64(const std::string_view sv);
BSResource::ID GetResourceIdFromPath(const std::string& path);
uint64_t GetHashFromPath(const std::string& path);
uint64_t GetHashFrom32(uint32_t val);
std::string GetFormatedResourceId(const BSResource::ID& id);