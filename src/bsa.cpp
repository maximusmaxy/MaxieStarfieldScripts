#include "bsa.h"

#include <bsa/fo4.hpp>

#include <string_view>
#include <span>
#include <spanstream>

#include "util.h"
#include "crc.h"
#include "nif.h"

bool GetMaterialDatabase(const std::string& path, std::vector<char>& bytes) {
	bsa::fo4::archive ba2;
	const auto version = ba2.read({ path });
	const auto file = ba2["materials/materialsbeta.cdb"];
	if (!file)
		return false;
	const auto& matDb = file->front();
	bytes.resize(matDb.decompressed_size());
	matDb.decompress_into({ (std::byte*)bytes.data(), bytes.size() }, (bsa::fo4::compression_format)file->header.format);
	return true;
}

bool GetMaterialPathsFromBsa(PathSet& pathSet, const std::string& path) {
	bsa::fo4::archive ba2;
	const auto version = ba2.read({ path });
	if (!ba2.size())
		return false;
	for (auto& [key, file] : ba2) {
		std::string name(key.name());
		if (HasExtension(name, ".nif")) {
			const auto& nif = file.front();
			std::vector<char> buffer(nif.decompressed_size());
			nif.decompress_into({ (std::byte*)buffer.data(), buffer.size() }, (bsa::fo4::compression_format)file.header.format);
			std::spanstream stream({ buffer.data(), buffer.size() });
			if (!GetMaterialPathsFromNifStream(pathSet, stream))
				Log("Failed to read compressed .nif file {} from {}", name, path);
		}
	}
	return true;
}