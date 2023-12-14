#pragma once

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <array>

inline constexpr size_t _FNV_offset_basis = 14695981039346656037ULL;
inline constexpr size_t _FNV_prime = 1099511628211ULL;

struct LowerHash {
	template <class Str>
	const size_t operator() (const Str& str) const
	{
		size_t val = _FNV_offset_basis;
		const Str::value_type* c = str.c_str();
		while (*c != 0) {
			val ^= static_cast<size_t>(std::tolower(*c));
			val *= _FNV_prime;
			++c;
		}
		return val;
	}

	template <class Char>
	const size_t operator() (const Char* str) const
	{
		size_t val = _FNV_offset_basis;
		while (*str != 0) {
			val ^= static_cast<size_t>(std::tolower(*str));
			val *= _FNV_prime;
			++str;
		}
		return val;
	}
};

struct InsensitiveEqual {
	const bool operator()(const std::string& lhs, const std::string& rhs) const
	{
		return _stricmp(lhs.c_str(), rhs.c_str()) == 0;
	}

	const bool operator()(const std::wstring& lhs, const std::wstring& rhs) const
	{
		return _wcsicmp(lhs.c_str(), rhs.c_str()) == 0;
	}

	const bool operator()(const char* lhs, const char* rhs) const
	{
		return _stricmp(lhs, rhs) == 0;
	}

	const bool operator()(const wchar_t* lhs, const wchar_t* rhs) const
	{
		return _wcsicmp(lhs, rhs) == 0;
	}
};

constexpr std::array<char, 256> GetLowerBackslashMap() {
	std::array<char, 256> result = {};
	for (int i = 0; i < 256; ++i) {
		result[i] = i;
	}
	for (int i = 'A'; i <= 'Z'; ++i) {
		result[i] = i + 0x20;
	}
	result['/'] = '\\';
	return result;
}

struct LowerBackslashHash {
	template <class Str>
	const size_t operator() (const Str& str) const
	{
		size_t val = _FNV_offset_basis;
		const Str::value_type* c = str.c_str();
		constexpr auto lowerBackslashMap = GetLowerBackslashMap();
		while (*c != 0) {
			val ^= static_cast<size_t>(lowerBackslashMap[static_cast<uint8_t>(*c)]);
			val *= _FNV_prime;
			++c;
		}
		return val;
	}

	template <class Char>
	const size_t operator() (const Char* str) const
	{
		size_t val = _FNV_offset_basis;
		constexpr auto lowerBackslashMap = GetLowerBackslashMap();
		while (*str != 0) {
			val ^= static_cast<size_t>(lowerBackslashMap[static_cast<uint8_t>(*str)]);
			val *= _FNV_prime;
			++str;
		}
		return val;
	}
};

struct LowerBackslashEqual {
	const bool operator()(const std::string& lhs, const std::string& rhs) const
	{
		auto lcstr = lhs.c_str();
		auto rcstr = rhs.c_str();
		constexpr auto lowerBackslashMap = GetLowerBackslashMap();
		while (*lcstr != 0 && *rcstr != 0) {
			if (lowerBackslashMap[*lcstr] != lowerBackslashMap[*rcstr])
				return false;
			++lcstr;
			++rcstr;
		}
		return lowerBackslashMap[*lcstr] == lowerBackslashMap[*rcstr];
	}
};

typedef std::unordered_set<std::string, LowerHash, InsensitiveEqual> InsensitiveSet;
typedef std::unordered_set<std::wstring, LowerHash, InsensitiveEqual> InsensitiveSetW;
template <class Str, class Value>
using InsensitiveMap = std::unordered_map<Str, Value, LowerHash, InsensitiveEqual>;

typedef std::unordered_set<std::string, LowerBackslashHash, LowerBackslashEqual> PathSet;
typedef std::unordered_set<std::wstring, LowerBackslashHash, LowerBackslashEqual> PathSetW;
template <class Str, class Value>
using PathMap = std::unordered_map<Str, Value, LowerBackslashHash, LowerBackslashEqual>;