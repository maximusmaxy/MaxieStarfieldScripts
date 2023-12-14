#include "util.h"

#include <algorithm>
#include "types.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "windows.h"

bool HasExtension(const std::string& str, const char* ext) {
	const auto last = str.find_last_of('.');
	return last != std::string::npos && _stricmp(str.c_str() + last, ext) == 0;
}

bool HasExtension(const std::wstring& str, const wchar_t* ext) {
	const auto last = str.find_last_of(L'.');
	return last != std::wstring::npos && _wcsicmp(str.c_str() + last, ext) == 0;
}

bool CreateDirectories(const std::string& path) {
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(path).remove_filename(), ec);
	return !ec;
}

void SanitizePrefixedPath(std::string& path, const std::string& prefix) {
	constexpr auto lowerBackslashMap = GetLowerBackslashMap();
	const auto it = std::search(path.begin(), path.end(), prefix.begin(), prefix.end(), [](const char lhs, const char rhs) {
		return lowerBackslashMap[lhs] == lowerBackslashMap[rhs];
	});

	if (it == path.begin()) {
		return;
	}
	else if (it == path.end()) {
		const auto front = path.front();
		if (front == '/' || front == '\\') {
			path.insert(0, prefix);
		}
		else {
			path.insert(0, prefix + '\\');
		}
	}
	else {
		path.erase(path.begin(), it);
	}
}

std::filesystem::path GetRegistryPath(const wchar_t* subkey, const wchar_t* value) {
	DWORD pathSize = MAX_PATH;
	std::wstring pathBuffer(pathSize, 0);

	auto status = RegGetValueW(
		HKEY_LOCAL_MACHINE,
		subkey,
		value,
		RRF_RT_REG_SZ,
		nullptr,
		(void*)pathBuffer.data(),
		&pathSize
	);

	return std::filesystem::path(status == ERROR_SUCCESS ? pathBuffer.c_str() : L"");
}