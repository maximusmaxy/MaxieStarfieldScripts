#pragma once

#include <string>
#include <string_view>
#include <format>
#include <iostream>
#include <filesystem>

bool HasExtension(const std::string& str, const char* ext);
bool HasExtension(const std::wstring& str, const wchar_t* ext);
bool CreateDirectories(const std::string& path);
void SanitizePrefixedPath(std::string& path, const std::string& prefix);
std::filesystem::path GetRegistryPath(const wchar_t* subkey, const wchar_t* value);

template<typename... Args>
void Log(std::format_string<Args...> fmt, Args&&... args) {
    std::cout << std::format(fmt, std::forward<Args>(args)...) << "\n";
}

template<typename... Args>
void Error(std::format_string<Args...> fmt, Args&&... args) {
    const auto message = std::format(fmt, std::forward<Args>(args)...);
    std::cout << message << "\n";
    throw std::exception("CDB Error");
}