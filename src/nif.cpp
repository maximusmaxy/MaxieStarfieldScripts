#include "nif.h"

#include <nlohmann/json.hpp>

#include "util.h"

#include <NifFile.hpp>
using namespace nifly;

void GetMaterialPathsFromNifFile(PathSet& result, nifly::NifFile& nif) {
	auto& header = nif.GetHeader();
	auto size = header.GetNumBlocks();
	for (uint32_t i = 0; i < size; ++i) {
		auto object = header.GetBlock<NiObjectNET>(i);
		if (object && HasExtension(object->name.get(), ".mat")) {
			auto matPath(object->name.get());
			SanitizePrefixedPath(matPath, "material");
			result.emplace(std::move(matPath));
		}
	}
}

bool GetMaterialPathsFromNifPath(PathSet& result, const std::filesystem::path& path) {
	nifly::NifFile nif;
	if (nif.Load(path) != 0)
		return false;
	GetMaterialPathsFromNifFile(result, nif);
	return true;
}

bool GetMaterialPathsFromNifStream(PathSet& result, std::istream& stream) {
	nifly::NifFile nif;
	if (nif.Load(stream) != 0)
		return false;
	GetMaterialPathsFromNifFile(result, nif);
	return true;
}

bool GetMaterialPathsFromNifsRecursive(PathSet& result, const std::filesystem::path& folder) {
	std::error_code ec;
	auto dirIt = std::filesystem::recursive_directory_iterator(folder, ec);
	if (ec)
		return false;

	for (const auto& entry : dirIt) {
		if (HasExtension(entry.path().native(), L".nif")) {
			GetMaterialPathsFromNifPath(result, entry.path());
		}
	}

	return true;
}