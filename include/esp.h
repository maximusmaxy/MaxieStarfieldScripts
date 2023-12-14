#pragma once

#include <cstdint>
#include <vector>
#include <fstream>
#include <span>
#include <spanstream>
#include <functional>
#include <filesystem>

#include <miniz/miniz.h>

namespace esp {
	constexpr uint32_t Sig(const char* val) {
		return (uint8_t)val[0] |
			(uint8_t)val[1] << 8 |
			(uint8_t)val[2] << 16 |
			(uint8_t)val[3] << 24;
	}

	struct Record {
		uint32_t sig;
		uint32_t size;
		uint32_t flags;
		uint32_t formId;
		uint32_t info1;
		uint16_t version;
		uint16_t info2;

		inline bool IsCompressed() const {
			return flags & (1 << 18);
		}
	};

	struct Group {
		uint32_t sig;
		uint32_t size;
		uint32_t value;
		uint32_t depth;
		uint32_t stamp;
		uint32_t unk;
	};

	union Header {
		Record record;
		Group group;
	};

	struct Element {
		uint32_t sig;
		uint16_t len;
	};

	class Reader {
		using Masters = std::vector<std::string>;
	private:
		std::ifstream filestream;
		std::vector<char> dstBuffer;
		std::ispanstream decompressStream{ std::span(dstBuffer.data(), dstBuffer.size()) };
		std::istream* in = nullptr;
		std::string filename;
		//Masters masters;

	public:
		Reader(const std::string& _filename) : filename(_filename) {
			filestream.open(std::filesystem::path("Data") / filename, std::ios::in | std::ios::binary);
			in = &filestream;
		}
		bool Fail() const {
			return filestream.fail();
		}
		bool Eof() const {
			return filestream.rdstate() & (std::ios::badbit | std::ios::failbit | std::ios::eofbit);
		}
		std::istream* Stream() const {
			return in;
		}

		inline void Skip(uint32_t offset) {
			in->seekg(offset, std::ios::cur);
		}

		template <class T = uint32_t>
		Reader& operator>>(T& rhs) {
			in->read(reinterpret_cast<char*>(&rhs), sizeof(T));
			return *this;
		}

		Reader& operator>>(std::string& str) {
			str.clear();
			char c = Get<char>();
			while (c != 0) {
				str.push_back(c);
				c = Get<char>();
			}
			return *this;
		}

		Reader& operator>>(Element& rhs) {
			*this >> rhs.sig >> rhs.len;
			return *this;
		}

		void Read(char* str, uint32_t len) {
			in->read(str, len);
		}

		template <class T = uint32_t>
		T Get() {
			T result;
			in->read(reinterpret_cast<char*>(&result), sizeof(T));
			return result;
		}

		void ForEachSig(uint32_t size, const std::function<void(Element& element)>& functor) {
			uint32_t count = 0;
			Element element;
			while (count < size) {
				*this >> element;
				count += (6 + element.len);
				functor(element);
			}
		}

		void ForEachRecord(const Group& group, const std::function<void(const Record& record)>& functor) {
			Record record;
			uint32_t count = 0;
			const auto size = group.size - 0x18;
			std::string buffer;
			while (count < size) {
				*this >> record;
				count += (0x18 + record.size);

				bool success = true;
				bool compressed = record.IsCompressed();
				if (compressed) {
					auto dstLen = Get();
					success = Inflate(record.size - 4, dstLen, buffer);
					record.size = dstLen;
				}

				if (success) {
					functor(record);

					if (compressed)
						EndInflate();
				}
			}
		}

		void ForEachGroup(const std::vector<uint32_t>& groupSigs, const std::function<void(const Group& group)>& functor) {
			Group group;
			while (!Eof()) {
				*this >> group;
				if (std::find(groupSigs.begin(), groupSigs.end(), group.value) != groupSigs.end()) {
					functor(group);
				}
				else {
					SkipGroup(group.size);
				}
			}
		}

		bool SeekToGroup(uint32_t sig) {
			Group group;
			while (!Eof()) {
				*this >> group;
				if (group.value != sig) {
					SkipGroup(group.size);
				}
				else {
					return true;
				}
			}
			return false;
		}

		bool SeekToGroup(uint32_t sig, Group& group) {
			while (!Eof()) {
				*this >> group;
				if (group.value != sig) {
					SkipGroup(group.size);
				}
				else {
					return true;
				}
			}
			return false;
		}

		//Returns the remaining length of the record after current element
		uint32_t SeekToElement(uint32_t len, uint32_t sig) {
			uint32_t count = 0;
			Element element;
			while (count < len) {
				*this >> element;
				count += 6;
				if (element.sig == sig)
					return len - count;
				count += element.len;
				Skip(element.len);
			}
			return 0;
		}

		uint32_t SeekToElement(uint32_t len, uint32_t sig, Element& element) {
			uint32_t count = 0;
			while (count < len) {
				*this >> element;
				count += 6;
				if (element.sig == sig)
					return len - count;
				count += element.len;
				Skip(element.len);
			}
			return 0;
		}

		uint32_t NextElement(uint32_t len, Element& element) {
			if (!len) {
				element = { 0, 0 };
				return len;
			}
			*this >> element;
			return (len - 6);
		}

		void ReadHeader() {
			Record header;
			*this >> header;

			std::string masterName;
			Element element;
			auto remaining = SeekToElement(header.size, Sig("MAST"), element);
			if (remaining > 0) {
				while (element.sig == Sig("MAST")) {
					remaining -= element.len;
					*this >> masterName;
					//auto info = GetModInfo(masterName.c_str());
					//if (info)
					//	masters.emplace_back(info);
					//else
					//	masters.emplace_back(ModInfo::Null());

					//Data
					remaining = NextElement(remaining, element);
					remaining -= element.len;
					Skip(element.len);

					remaining = NextElement(remaining, element);
				}
				Skip(remaining);
			}
			//masters.emplace_back(filename);
		}

		//inline uint32_t GetFormId(uint32_t formId) {
		//	const auto index = formId >> 24;
		//	if (index >= masters.size())
		//		return 0;
		//	const auto& info = masters.at(index);
		//	if (info.modIndex == 0xFF000000)
		//		return 0;
		//	return info.modIndex | (formId & info.formMask);
		//}

		//inline uint32_t ReadFormId() {
		//	return GetFormId(Get());
		//}

		inline void SkipGroup(uint32_t len) {
			Skip(len - 0x18);
		}

		//For single form access only
		bool SeekToFormId(uint32_t formId, uint32_t signature, Record& record) {
			Group group;
			if (!SeekToGroup(signature, group))
				return false;

			uint32_t count = 0;
			const auto size = group.size - 0x18;
			while (count < size) {
				*this >> record;
				count += (0x18 + record.size);
				if (record.formId == formId) {
					if (record.IsCompressed()) {
						std::string buffer;
						auto dstLen = Get();
						Inflate(record.size - 4, dstLen, buffer);
						record.size = dstLen;
					}
					return true;
				}
			}

			return false;
		}

		//There's probably a better way to do this
		bool Inflate(uint32_t srcLen, unsigned long dstLen, std::string& srcBuffer) {
			srcBuffer.resize(srcLen);
			filestream.read(srcBuffer.data(), srcLen);

			dstBuffer.resize(dstLen);
			if (uncompress((uint8_t*)dstBuffer.data(), &dstLen, (uint8_t*)srcBuffer.data(), srcLen) != 0)
				return false;

			decompressStream = std::ispanstream(std::span(dstBuffer.data(), dstBuffer.size()));
			in = &decompressStream;

			return true;
		}

		void EndInflate() {
			//stringstream.clear();
			in = &filestream;
		}
	};
}