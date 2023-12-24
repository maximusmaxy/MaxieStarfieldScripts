#include "util.h"
#include "crc.h"

#include <filesystem>

void LogCrc(const std::string& path) {
	if (HasExtension(path, ".mat")) {
		std::string sanitizedPath(path);
		SanitizePrefixedPath(sanitizedPath, "materials");
		Log("{}: {}", sanitizedPath, GetCrc(sanitizedPath));
	}
	else {
		Log("{}: {}", path, GetCrc(path));
	}
}

int main(int argc, char** argv) {
	if (argc == 1) {
		Log("Drag .mat files onto this .exe to get the MaterialID");
	}
	for (int i = 1; i < argc; ++i) {
		std::string path(argv[i]);
		if (std::filesystem::is_directory(path)) {
			std::error_code ec;
			auto it = std::filesystem::recursive_directory_iterator(path, ec);
			if (!ec) {
				for (auto& entry : it) {
					if (entry.is_regular_file())
						LogCrc(entry.path().string());
				}
			}
			else {
				Log("Error reading folder {}", ec.message());
			}
		}
		else {
			LogCrc(path);
		}
	}
	auto _ = getchar();
	return 0;
}