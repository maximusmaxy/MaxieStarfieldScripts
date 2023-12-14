#pragma once

#include <fstream>
#include <filesystem>
#include <stdint.h>
#include <set>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <queue>
#include <format>
#include <array>

#include <nlohmann/json.hpp>

#include "crc.h"
#include "bs.h"
#include "util.h"

namespace cdb {
	struct TestStruct {
		std::set<uint32_t> objectSet;
		std::set<uint32_t> componentSet;
		std::set<uint32_t> edgeSet;
		BSComponentDB2::ID matId;
		BSResource::ID resourceId;
		uint64_t hash;
	};

	struct Manager {
		using Database = BSMaterial::Internal::CompiledDB;
		using FileIndex = BSComponentDB2::DBFileIndex;
		using ObjectMap = std::unordered_map<uint32_t, const FileIndex::ObjectInfo&>;
		struct ComponentRef {
			const FileIndex::ComponentInfo& component;
			uint32_t idx;
			uint32_t pos;
		};
		using ComponentMap = std::unordered_map<uint32_t, std::vector<ComponentRef>>;
		struct EdgeRef {
			const FileIndex::EdgeInfo& edge;
			uint32_t idx;
		};
		using EdgeMap = std::unordered_map<uint32_t, std::vector<EdgeRef>>;

		Database database;
		FileIndex fileIndex;
		ObjectMap objectMap;
		ComponentMap componentMap;
		EdgeMap edgeMap;
		uint32_t nextObjectId;
		std::vector<nlohmann::json> componentJsons;
		std::unordered_map<std::string, nlohmann::json> classJsons;
		std::vector<uint32_t> posMap;
		std::unordered_map<BSResource::ID, BSComponentDB2::ID> resourceToDb;

		FileIndex::ObjectInfo emptyObject{ {0}, 0, 0, false };
		const FileIndex::ObjectInfo& GetObject(const BSComponentDB2::ID id) const {
			auto it = objectMap.find(id.Value);
			return it != objectMap.end() ? it->second : emptyObject;
		}

		ComponentMap::mapped_type emptyComponentList;
		const ComponentMap::mapped_type& GetComponents(const BSComponentDB2::ID id) const {
			auto it = componentMap.find(id.Value);
			return it != componentMap.end() ? it->second : emptyComponentList;
		}

		const FileIndex::ComponentTypeInfo& GetType(const uint16_t typeId) const {
			auto it = std::find_if(fileIndex.ComponentTypes.begin(), fileIndex.ComponentTypes.end(), [typeId](const auto& type) {
				return type.first == typeId;
			});
			return it->second;
			//return fileIndex.ComponentTypes.at(typeId);
		};

		uint16_t GetTypeIndex(const std::string& typeName) const {
			auto it = std::find_if(fileIndex.ComponentTypes.begin(), fileIndex.ComponentTypes.end(), [typeName](const auto& type) {
				return _stricmp(type.second.Class.c_str(), typeName.c_str()) == 0;
			});
			return it != fileIndex.ComponentTypes.end() ? it->first : 0;
		}

		//const FileIndex::ComponentTypeInfo& GetType(const char* typeName) const {
		//	auto it = std::find_if(fileIndex.ComponentTypes.begin(), fileIndex.ComponentTypes.end(), [typeName](const auto& type) {
		//		return _stricmp(type.second.Class.c_str(), typeName) == 0;
		//	});
		//	return it != fileIndex.ComponentTypes.end() ? it->second : 
		//}

		const EdgeMap::mapped_type emptyEdges;
		const EdgeMap::mapped_type& GetEdges(const BSComponentDB2::ID id) const {
			auto it = edgeMap.find(id.Value);
			return it != edgeMap.end() ? it->second : emptyEdges;
		}

		std::vector<BSComponentDB2::ID> GetParentList(const BSComponentDB2::ID id) const {
			std::vector<BSComponentDB2::ID> result;
			auto object = &GetObject(id);
			result.emplace_back(id);
			while (object->Parent.Value != 0) {
				result.emplace_back(object->Parent);
				object = &GetObject(object->Parent);
			}
			return result;
		};

		struct ObjectQueue {
			std::map<uint32_t, uint32_t> idMap;
			std::vector<std::pair<uint32_t, uint32_t>> idQueue;
			std::vector<uint32_t> dbIds;

			uint32_t nextId = 1;
			//uint32_t nextEdge = 0;
			size_t Size() const { return idQueue.size(); }

			uint32_t Push(uint32_t id) {
				auto it = idMap.emplace(id, 0);
				if (it.second) {
					it.first->second = nextId++;
					idQueue.emplace_back(id, it.first->second);
				}
				return it.first->second;
			}

			std::pair<uint32_t, uint32_t> Pop() {
				auto&& result = std::move(idQueue.back());
				idQueue.pop_back();
				return result;
			}
		};

		nlohmann::json& GetIndexedComponent(nlohmann::json& objectValue, const nlohmann::json& dbValue, uint32_t index) {
			assert(dbValue.contains("Type"));
			const std::string& dbTypeString = dbValue["Type"];

			for (auto& member : objectValue) {
				assert(member.contains("Index"));
				assert(member.contains("Type"));
				if (member["Index"] == index) {
					const std::string& memberTypeString = member["Type"];
					if (_stricmp(memberTypeString.c_str(), dbTypeString.c_str()) == 0) {
						return member;
					}
				}
			}

			//auto typeIt = classJsons.find(dbTypeString);
			//if (typeIt == classJsons.end())
			//	Error("Default type not found for component {}", dbTypeString);

			auto& emplaced = objectValue.emplace_back();
			//emplaced = typeIt->second;
			emplaced["Type"] = dbTypeString;
			emplaced["Index"] = index;
			return emplaced;
		};

		void GetFullJson(const BSComponentDB2::ID id, nlohmann::json& objectValue) {
			auto& componentsValue = objectValue["Components"];
			componentsValue = nlohmann::json::array();

			const auto parentList = GetParentList(id);
			for (auto idIt = parentList.rbegin(); idIt != parentList.rend(); ++idIt) {
				auto& components = GetComponents(*idIt);
				for (auto& ref : components) {
					auto& dbValue = componentJsons.at(ref.idx);
					auto& componentValue = GetIndexedComponent(componentsValue, dbValue, ref.component.Index);

					auto& dbData = dbValue["Data"];
					auto& componentData = componentValue["Data"];

					ComposeJsons(componentData, dbData);
				}
			}
		};

		void GetDiffJson(const BSComponentDB2::ID id, nlohmann::json& objectValue) {
			auto& componentsValue = objectValue["Components"];
			componentsValue = nlohmann::json::array();

			auto& components = GetComponents(id);
			for (auto& ref : components) {
				auto& dbValue = componentJsons.at(ref.idx);
				auto& componentValue = GetIndexedComponent(componentsValue, dbValue, ref.component.Index);

				auto& dbData = dbValue["Data"];
				auto& componentData = componentValue["Data"];

				ComposeJsons(componentData, dbData);
			}
		};

		//TODO get this dynamically as more types may be added
		static constexpr std::array idTypes = {
			"BSMaterial::BlenderID",
			"BSMaterial::LayerID",
			"BSMaterial::MaterialID",
			"BSMaterial::TextureSetID",
			"BSMaterial::UVStreamID",
			"BSMaterial::LODMaterialID",
			"BSMaterial::LayeredMaterialID",
		};

		bool IsComponentReference(const nlohmann::json& componentValue) const {
			return std::find(idTypes.begin(), idTypes.end(), componentValue["Type"]) != idTypes.end();
		}

		void GetReferencedIds(nlohmann::json& value, ObjectQueue& objectQueue) {
			if (IsComponentReference(value)) {
				auto& dbValue = value["Data"]["ID"];
				if (dbValue.is_string()) {
					const std::string& idStr = dbValue;
					if (idStr.size()) {
						BSComponentDB2::ID idValue = { std::stoul(idStr) };
						const auto& idObject = GetObject(idValue);
						if (idObject) {
							objectQueue.Push(idValue.Value);
							dbValue = GetFormatedResourceId(idObject.PersistentID);
						}
						else {
							Log("Object not found for id {}", idValue.Value);
						}
						//if (idValue != 0) {
						//	const auto localId = objectQueue.Push(idValue);
						//	dbValue = std::to_string(localId);
						//}
					}
				}
				else
					Log("Database ID was not an string");
			}
			else {
				auto& data = value["Data"];
				for (auto& member : data) {
					if (member.is_object() && member.contains("Type")) {
						GetReferencedIds(member, objectQueue);
					}
				}
			}
		};

		BSComponentDB2::ID GetMatId(const std::string& path) {
			auto matResourceId = GetResourceIdFromPath(path);
			auto pathIt = resourceToDb.find(matResourceId);
			return pathIt != resourceToDb.end() ? pathIt->second : BSComponentDB2::ID{ 0 };
		};

		void SetMaterialParent(nlohmann::json& matJson, const std::unordered_map<uint32_t, std::string>& matPathMap,
			const BSComponentDB2::ID matId) 
		{
			const auto parentList = GetParentList(matId);
			for (auto parentIt = parentList.begin() + 1; parentIt != parentList.end(); ++parentIt) {
				auto pathIt = matPathMap.find(parentIt->Value);
				if (pathIt != matPathMap.end()) {
					matJson["Parent"] = pathIt->second;
					return;
				}
			}
			Error("Failed to find a parent for object {:08X}", matId.Value);
		}

		void CreateMaterialJson(nlohmann::json& matJson, const BSComponentDB2::ID matId,
			const std::unordered_map<uint32_t, std::string>& idToPath)
		{
			ObjectQueue objectQueue;
			matJson["Version"] = 1;
			auto& objects = matJson["Objects"];
			auto& matObject = objects.emplace_back();
			//objectQueue.dbIds.emplace_back(matId.Value);
			objectQueue.idMap.emplace(matId.Value, 0);
			//matObject["ID"] = std::to_string(objectQueue.nextId++);
			//matObject["ID"] = "<this>";
			GetFullJson(matId, matObject);
			SetMaterialParent(matObject, idToPath, matId);
			for (auto& component : matObject["Components"]) {
				GetReferencedIds(component, objectQueue);
			}

			while (objectQueue.Size()) {
				auto& refObject = objects.emplace_back();
				auto [dbId, localId] = objectQueue.Pop();
				objectQueue.dbIds.emplace_back(dbId);
				const auto& dbObject = GetObject({ dbId });
				if (dbObject) {
					refObject["ID"] = GetFormatedResourceId(dbObject.PersistentID);
					GetFullJson({ dbId }, refObject);
					SetMaterialParent(refObject, idToPath, { dbId });
					for (auto& component : refObject["Components"]) {
						GetReferencedIds(component, objectQueue);
					}
				}
			}

			////I don't think edges are needed
			//for (uint32_t i = 0; i < objectQueue.dbIds.size(); ++i) {
			//    uint32_t dbId = objectQueue.dbIds[i];
			//    auto& objectValue = objects[i + 1];
			//	auto& edges = GetEdges({ dbId });
			//	for (auto& ref : edges) {
			//		auto idIt = objectQueue.idMap.find(ref.edge.TargetID.Value);
			//		//auto idIt = objectQueue.idMap.find(ref.edge.TargetId.Value);
			//		if (idIt != objectQueue.idMap.end()) {
			//			auto& edgeValue = objectValue["Edges"].emplace_back();
			//			edgeValue["Type"] = "BSComponentDB2::OuterEdge";
			//			edgeValue["To"] = idIt->second == 0 ? "<this>" : std::to_string(idIt->second);
			//			if (ref.edge.Index != 0)
			//				edgeValue["EdgeIndex"] = ref.edge.Index;
			//		}
			//	}
			//}
		}

		void UpdateDatabaseIds(nlohmann::json& matValue, const std::string& path) {
			std::map<uint32_t, uint32_t> idMap;
			auto& objectsValue = matValue["Objects"];
			for (auto& object : objectsValue) {
				if (object.contains("ID")) {
					const std::string& idString = object["ID"];
					uint32_t localId = std::stoul(idString);
					const auto emplacedId = idMap.emplace(localId, nextObjectId++);
					object["ID"] = std::to_string(emplacedId.first->second);
				}
				else {
					object["ID"] = std::to_string(nextObjectId++);
				}
			}
			for (auto& object : objectsValue) {
				auto& componentsValue = object["Components"];
				for (auto& component : componentsValue) {
					if (IsComponentReference(component)) {
						auto& componentValue = component["Data"]["ID"];
						const std::string& idString = componentValue;
						uint32_t localId = std::stoul(idString);
						auto idIt = idMap.find(localId);
						if (idIt != idMap.end()) {
							componentValue = std::to_string(idIt->second);
						}
						else {
							Log("Component id referenced a missing object id {}, for material {}", idString, path);
						}
					}
				}
			}
		}

		void GetComponentIndexesForMaterial(BSComponentDB2::ID id, TestStruct& tester) {
			const auto parentList = GetParentList(id);
			for (auto idIt = parentList.rbegin(); idIt != parentList.rend(); ++idIt) {
				if (tester.objectSet.emplace(idIt->Value).second) {
					auto& components = GetComponents(*idIt);
					for (auto& ref : components) {
						const auto& type = GetType(ref.component.Type);
						if (std::find(idTypes.begin(), idTypes.end(), type.Class) != idTypes.end()) {
							const std::string& idStr = componentJsons.at(ref.idx)["Data"]["ID"];
							uint32_t objectId = std::stoul(idStr);
							GetComponentIndexesForMaterial({ objectId }, tester);
						}
						tester.componentSet.emplace(ref.idx);
					}
					auto& edges = GetEdges(*idIt);
					for (auto& ref : edges) {
						if (tester.edgeSet.emplace(ref.idx).second) {
							GetComponentIndexesForMaterial( ref.edge.TargetID , tester);
						}
					}
				}
			}
		}

		void ComposeJsons(nlohmann::json& lhs, const nlohmann::json& rhs) {
			if (rhs.is_object()) {
				if (rhs.empty()) {
					lhs = nlohmann::json::object();
				}
				else if (lhs.is_string()) {
					const std::string& str = lhs;
					Log("{}", str);
				}
				else {
					for (auto it = rhs.begin(); it != rhs.end(); ++it) {
						ComposeJsons(lhs[it.key()], *it);
					}
				}
			}
			else if (rhs.is_array()) {
				if (rhs.empty()) {
					lhs = nlohmann::json::array();
				}
				else {
					for (uint32_t i = 0; i < rhs.size(); ++i) {
						if (!rhs[i].is_null())
							ComposeJsons(lhs[i], rhs[i]);
					}
				}
			}
			else {
				lhs = rhs;
			}
		};

		bool CompareJsons(const nlohmann::json& lhs, const nlohmann::json& rhs) const {
			if (rhs.is_object()) {
				if (!lhs.is_object())
					return false;
				for (auto it = rhs.begin(); it != rhs.end(); ++it) {
					if (!CompareJsons(lhs[it.key()], *it))
						return false;
				}
			}
			else if (rhs.is_array()) {
				if (!lhs.is_array())
					return false;
				for (size_t i = 0; i < rhs.size(); ++i) {
					if (!CompareJsons(lhs[i], rhs[i]))
						return false;
				}
			}
			else {
				if (lhs != rhs)
					return false;
			}
			return true;
		}
	};

	struct StringRef {
		uint32_t data;
	};

	struct TypeRef {
		uint32_t data;

		enum BuiltIn : uint32_t {
			Null = 0xFFFFFF01u,
			String = 0xFFFFFF02u,
			List = 0xFFFFFF03u,
			Map = 0xFFFFFF04u,
			Ref = 0xFFFFFF05u,
			Int8 = 0xFFFFFF08u,
			UInt8 = 0xFFFFFF09u,
			Int16 = 0xFFFFFF0Au,
			UInt16 = 0xFFFFFF0Bu,
			Int32 = 0xFFFFFF0Cu,
			UInt32 = 0xFFFFFF0Du,
			Int64 = 0xFFFFFF0Eu,
			UInt64 = 0xFFFFFF0Fu,
			Bool = 0xFFFFFF10u,
			Float = 0xFFFFFF11u,
			Double = 0xFFFFFF12u,
			Npos = 0xFFFFFFFFu,
		};

		static constexpr std::array builtinStrings{
			"Unk0",
			"<null>",
			"BSFixedString",
			"<collection>", //List
			"<collection>", //Map
			"pointer",
			"Unk6",
			"Unk7",
			"int8_t",
			"uint8_t",
			"int16_t",
			"uint16_t",
			"int32_t",
			"uint32_t",
			"int64_t",
			"uint64_t",
			"bool",
			"float",
			"double"
		};

		static constexpr std::array testStrings{ 1, 2, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18 };

		inline bool IsBuiltin() const { return (data & 0xFFFFFF00) == 0xFFFFFF00; }
		inline bool IsChunk() const { return data == List || data == Map; }

		operator bool() const { return data != Npos; }

		bool operator==(const TypeRef rhs) const {
			return data == rhs.data;
		}
		bool operator==(const uint32_t rhs) const {
			return data == rhs;
		}
	};

	struct User {
		TypeRef target;
		TypeRef casted;
	};

	struct Chunk {
		uint32_t sig;
		uint32_t size;

		enum Sig : uint32_t {
			BETH = 'HTEB',
			OBJT = 'TJBO',
			USER = 'RESU',
			DIFF = 'FFID',
			USRD = 'DRSU',
			MAPC = 'CPAM',
			LIST = 'TSIL',
		};

		inline bool IsDiff() const { return sig == DIFF || sig == USRD; }
		inline bool IsUser() const { return sig == USER || sig == USRD; }
		inline bool IsType() const { return sig == OBJT || sig == DIFF; }
		inline bool IsList() const { return sig == LIST; }
		inline bool IsMap() const { return sig == MAPC; }
		inline std::string_view GetSig() const { return { reinterpret_cast<const char*>(&sig), 4 }; }
	};

	struct Map {
		TypeRef key;
		TypeRef value;
		uint32_t size;
	};

	struct List {
		TypeRef type;
		uint32_t size;
	};

	struct Class {

		enum Flags : uint32_t {
			None = 0,
			User = 1 << 2,
			Struct = 1 << 3,

			Null = 0xFFFFu,
		};

		struct Field {
			StringRef name;
			TypeRef typeId;
			uint16_t offset;
			uint16_t size;

			operator bool() const { return offset != Null; }
		};

		StringRef name{};
		TypeRef typeId{0};
		uint16_t flags = Null;
		uint16_t fieldSize = 0;
		std::vector<Field> fields;

		operator bool() const { return flags != Null; }
		inline bool IsUser() const { return flags & User; }
		inline bool IsStruct() const { return flags & Struct; }
	};

	class Reader {
	public:
		struct QueuedChunk {
			nlohmann::json& value;
			bool isDiff;
		};

		struct QueuedCast {
			nlohmann::json& value;
			TypeRef type;
		};

	protected:
		std::istream& in;
		std::vector<char> stringTable;
		std::vector<Class> classes;
		uint32_t version = 0;
		uint32_t chunkSize = 0;
		uint32_t headerChunkSize = 0;
		uint32_t chunksRemaining = 0;
		uint32_t userValue = 0;

		std::vector<QueuedChunk> chunkQueue;
		std::vector<QueuedCast> userQueue;

	public:
		Reader(std::istream& _in) : in(_in) {}

		inline bool End() const { return !chunksRemaining || in.eof(); }
		inline void Skip(uint32_t offset) { in.seekg(offset, std::ios::cur); }
		inline uint32_t Pos() const { return (uint32_t)in.tellg(); }
		inline const std::vector<char>& StringTable() { return stringTable; }
		inline const std::vector<Class>& Classes() { return classes; }
		inline uint32_t& HeaderChunkSize() { return headerChunkSize; }
		inline uint32_t& ChunkSize() { return chunkSize; }
		inline uint32_t& Version() { return version; };
		inline uint32_t& ChunksRemaining() { return chunksRemaining; }

		class Exception : public std::exception {
		private:
			std::string message;
		public:
			Exception(std::string&& _message) : message(_message) {}
			const char* what() const { return message.c_str(); }
		};

		template<typename... Args>
		void Error(std::format_string<Args...> fmt, Args&&... args) {
			std::cout << "Pos: " << std::uppercase << std::hex << Pos() << "\n";
			throw Exception(std::format(fmt, std::forward<Args>(args)...));
		}

		std::istream& Stream() const {
			return in;
		}

		template <class T = uint32_t>
		Reader& operator>>(T& rhs) {
			in.read(reinterpret_cast<char*>(&rhs), sizeof(T));
			return *this;
		}

		template <class T = uint32_t>
		T Read() {
			T result;
			in.read(reinterpret_cast<char*>(&result), sizeof(T));
			return result;
		}

		template <class T = uint32_t>
		std::string ReadString() {
			T result;
			in.read(reinterpret_cast<char*>(&result), sizeof(T));
			return std::to_string(result);
		}

		template <>
		std::string ReadString<std::string>() {
			std::string result;
			uint16_t len = Read<uint16_t>();
			result.resize(len - 1);
			in.read(result.data(), len);
			return result;
		}

		template <>
		Chunk Read<Chunk>() {
			Chunk result;
			*this >> result.sig >> result.size;
			chunksRemaining--;
			return result;
		}

		Reader& operator>>(Chunk& chunk) {
			chunk = Read<Chunk>();
			return *this;
		}

		Reader& operator>>(Map& map) {
			*this >> map.key >> map.value >> map.size;
			return *this;
		}

		Reader& operator>>(nullptr_t& ptr) {
			ptr = nullptr;
			return *this;
		}

		Reader& operator>>(std::string& str) {
			uint16_t size;
			*this >> size;
			if (size == 0) {
				str.clear();
			}
			else {
				str.resize(size - 1);
				in.read(str.data(), size);
			}
			return *this;
		}

		template <class T, class U>
		Reader& operator>>(std::pair<T, U>& rhs) {
			return *this >> rhs.first >> rhs.second;
		}

		template <class T>
		Reader& operator>>(std::vector<T>& rhs) {
			using value_t = std::vector<T>::value_type;
			Chunk chunk = Read<Chunk>();
			List list = GetList();
			for (uint32_t i = 0; i < list.size; ++i) {
				value_t value;
				*this >> value;
				rhs.emplace_back(std::move(value));
			}
			return *this;
		}

		template <class T, class U>
		Reader& operator>>(std::unordered_map<T, U>& rhs) {
			Chunk chunk = Read<Chunk>();
			Map map = GetMap();
			for (uint32_t i = 0; i < map.size; ++i) {
				T key;
				U value;
				*this >> key >> value;
				rhs.emplace(std::move(key), std::move(value));
			}
			return *this;
		}

		Reader& operator>>(BSResource::ID& id) {
			return *this >> id.file >> id.ext >> id.dir;
		}

		Reader& operator>>(BSMaterial::Internal::CompiledDB& db) {
			uint32_t pad;
			return *this >> db.BuildVersion >> pad >> db.HashMap >> db.Collisions >> db.Circular;
		}

		Reader& operator>>(BSMaterial::Internal::CompiledDB::FilePair& pair) {
			return *this >> pair.First >> pair.Second;
		}

		struct TypeInfoPartial {
			uint16_t version;
			bool isEmpty;
		};
		Reader& operator>>(TypeInfoPartial& info) {
			return *this >> info.version >> info.isEmpty;
		}

		Reader& operator>>(BSComponentDB2::DBFileIndex& idx) {
			*this >> idx.Optimized;

			std::vector<std::pair<uint16_t, TypeInfoPartial>> typeVec;
			uint32_t pad = Read<uint32_t>(); //It's reading a map so pad to make the size correct
			*this >> typeVec;
			for (const auto& [key, val] : typeVec) {
				Chunk chunk = Read<Chunk>();
				User user = Read<User>();
				std::string className;
				*this >> className;
				idx.ComponentTypes.emplace_back(key, BSComponentDB2::DBFileIndex::ComponentTypeInfo{
					 std::move(className), val.version, val.isEmpty
					});
				Read();
			}
			return *this >> idx.Objects >> idx.Components >> idx.Edges;
		}

		//Reader& operator>>(BSComponentDB2::DBFileIndex::ComponentTypeInfo& info) {
		//	return *this >> info.Class >> info.Version >> info.IsEmpty;
		//}

		Reader& operator>>(BSComponentDB2::DBFileIndex::ObjectInfo& info) {
			return *this >> info.PersistentID >> info.DBID >> info.Parent >> info.HasData;
		}

		Reader& operator>>(BSComponentDB2::DBFileIndex::ComponentInfo& info) {
			return *this >> info.ObjectID >> info.Index >> info.Type;
		}

		Reader& operator>>(BSComponentDB2::DBFileIndex::EdgeInfo& info) {
			return *this >> info.SourceID >> info.TargetID >> info.Index >> info.Type;
		}

		const char* GetString(StringRef ref) {
			return stringTable.data() + ref.data;
		}

		const char* GetString(TypeRef ref) {
			return ref.IsBuiltin() ? TypeRef::builtinStrings.at(ref.data & 0xFF) : GetString(GetType(ref).name);
		}

		const Class emptyClass{};
		const Class& GetType(TypeRef offset) {
			for (auto& type : classes) {
				if (type.name.data == offset.data)
					return type;
			}
			return emptyClass;
		}

		const Class& GetType(const char* typeName) {
			for (auto& type : classes) {
				if (strcmp(GetString(type.name), typeName) == 0)
					return type;
			}
			return emptyClass;
		}

		bool IsType(const char* typeName, TypeRef ref) {
			for (auto& type : classes) {
				if (strcmp(GetString(type.name), typeName) == 0)
					return true;
			}
			return false;
		}

		const TypeRef GetTypeRef(const char* typeName) {
			for (auto& type : classes) {
				if (strcmp(GetString(type.name), typeName) == 0)
					return TypeRef{ type.name.data };
			}
			return { TypeRef::Npos };
		}

		const TypeRef GetTypeRef(const Chunk chunk) {
			TypeRef result;
			if (!chunk.IsUser()) {
				*this >> result;
			}
			else {
				User user;
				*this >> user;
				result = user.casted;
			}
			return result;
		}

		void LogClasses(std::ostream& out) {
			for (auto& type : classes) {
				out << GetString(type.name);
				if (type.fields.size()) {
					out << ":\n";
					for (auto& field : type.fields) {
						out << "  " << GetString(field.name) << ": " << GetString(field.typeId) << "\n";
					}
				}
				else {
					out << ": ~\n";
				}
			}
		}

		void SkipNextObject() {
			Chunk chunk = Read<Chunk>();
			Skip(chunk.size);
			char peek = in.peek();
			while (!End() && peek != 'O' && peek != 'D') {
				Chunk chunk = Read<Chunk>();
				Skip(chunk.size);
				peek = in.peek();
			}
		}

		Map GetMap() {
			Map result;
			*this >> result.key >> result.value >> result.size;
			return result;
		}

		List GetList() {
			List result;
			*this >> result;
			return result;
		}

		void ReadHeader() {
			Chunk chunk;
			*this >> chunk.sig >> chunk.size >> version >> chunkSize;
			chunksRemaining = chunkSize - 1;
			
			*this >> chunk;
			stringTable.resize(chunk.size);
			in.read(stringTable.data(), chunk.size);

			*this >> chunk;
			uint32_t typeSize = Read();

			for (uint32_t i = 0; i < typeSize; ++i) {
				*this >> chunk;
				Class type;
				*this >> type.name >> type.typeId >> type.flags >> type.fieldSize;
				type.fields.reserve(type.fieldSize);
				for (uint32_t i = 0; i < type.fieldSize; ++i) {
					Class::Field field;
					*this >> field;
					type.fields.emplace_back(std::move(field));
				}
				const auto& emplaced = classes.emplace_back(std::move(type));
			}

			//std::sort(classes.begin(), classes.end(), [](const Class& lhs, const Class& rhs) {
			//	return lhs.name.data < rhs.name.data;
			//});

			headerChunkSize = 3 + typeSize;
			//chunksRemaining = chunkSize - headerChunkSize;
		}

		bool ReadHeader(Manager& header) {
			ReadHeader();

			constexpr std::array dbTypeMap{
				"BSMaterial::Internal::CompiledDB",
				"BSComponentDB2::DBFileIndex",
			};

			enum DbType {
				CompiledDB = 0,
				DBFileIndex,
				Npos = 0xFFFFFFFFu,
			};

			try {
				for (int i = 0; i < 2; ++i) {
					Chunk chunk = Read<Chunk>();
					auto ref = Read<TypeRef>();
					auto typeName = GetString(ref);
					uint32_t dbType = DbType::Npos;
					for (uint32_t i = 0; i < dbTypeMap.size(); ++i) {
						if (_stricmp(dbTypeMap[i], typeName) == 0) {
							dbType = i;
							break;
						}
					}
					switch (dbType) {
					case DbType::CompiledDB: *this >> header.database; break;
					case DbType::DBFileIndex: *this >> header.fileIndex; break;
					default:
						Error("Unknown db type: {}", typeName);
						return false;
					}
				}
			}
			catch (const std::exception& e) {
				Log("{}", e.what());
				return false;
			}

			header.objectMap.reserve(header.fileIndex.Objects.size());
			for (const auto& object : header.fileIndex.Objects) {
				header.objectMap.emplace(object.DBID.Value, object);
				if (object.DBID.Value > header.nextObjectId)
					header.nextObjectId = object.DBID.Value;
			}
			header.nextObjectId++;

			for (uint32_t i = 0; i < header.fileIndex.Components.size(); ++i) {
				const auto& component = header.fileIndex.Components.at(i);
				header.componentMap[component.ObjectID.Value].emplace_back(component, i);
			}

			for (uint32_t i = 0; i < header.fileIndex.Edges.size(); ++i) {
				const auto& edge = header.fileIndex.Edges.at(i);
				header.edgeMap[edge.SourceID.Value].emplace_back(edge, i);
				//header.edgeMap[edge.TargetID.Value].emplace_back(edge, i);
			}

			for (const auto& object : header.fileIndex.Objects) {
				if (object.PersistentID.ext == 'tam') {
					header.resourceToDb.emplace(object.PersistentID, object.DBID);
				}
			}

			//for (const auto& type : classes) {
			//	nlohmann::json classJson;
			//	GetDefaultType(classJson, { type.name.data });
			//	header.classJsons.emplace(GetString(type.name), std::move(classJson));
			//}
			return true;
		}

		bool ReadAllComponents(Manager& header) {
			try {
				header.posMap.reserve(header.fileIndex.Components.size());
				for (int i = 0; i < header.fileIndex.Components.size(); ++i) {
					header.posMap.emplace_back((uint32_t)in.tellg());
					auto& component = header.fileIndex.Components.at(i);
					auto& emplaced = header.componentJsons.emplace_back(nlohmann::json::object());
					ReadNextObject(emplaced);
				}
			}
			catch (std::exception& e) {
				Log("{}", e.what());
				return false;
			}
			return true;
		}

		void ReadAllChunks(nlohmann::json& value) {
			while (!End()) {
				ReadChunk(value);
			}
		}

		void ReadNextObject(nlohmann::json& value) {
			ReadChunk(value);
			while (chunkQueue.size() || userQueue.size()) {
				ReadChunk(value);
			}
		}

		void ReadList(nlohmann::json& value, bool isDiff) {
			List arr = GetList();
			value["Type"] = "<collection>";
			if (arr.size != 0) {
				value["ElementType"] = GetString(arr.type);
				auto& dataValue = value["Data"];
				for (uint32_t i = 0; i < arr.size; ++i) {
					auto& emplaced = dataValue.emplace_back();
					ReadType(emplaced, arr.type, isDiff);
				}
			}
			else {
				value["Data"] = nlohmann::json::array();
			}
		}

		//static constexpr std::array typeMapKeys{
		//	"BSResource::ID",
		//	"BSComponentDB2::ID",
		//};

		//enum TypeMap : uint32_t {
		//	BSResourceID = 0,
		//	BSComponentDB2ID,
		//	Npos = 0xFFFFFFFFu,
		//};

		//uint32_t GetTypeEnum(const TypeRef ref) {
		//	const char* typeName = stringTable.data() + ref.data;
		//	for (uint32_t i = 0; i < typeMapKeys.size(); ++i) {
		//		if (_stricmp(typeMapKeys[i], typeName) == 0)
		//			return i;
		//	}
		//	return Npos;
		//}

		void ReadMap(nlohmann::json& value, bool isDiff) {
			Map map = GetMap();

			//value = nlohmann::json::object();
			value["Type"] = "<collection>";
			//value["Type"] = "std::map";
			const char* elementTypeName = GetString(map.value);
			value["ElementType"] = "StdMapType::Pair";
			auto& dataValue = value["Data"];

			const auto& GetKeyString = [this](TypeRef ref) -> std::string {
				switch (ref.data) {
				case TypeRef::String: return ReadString<std::string>();
				case TypeRef::Int8: return ReadString<int8_t>();
				case TypeRef::UInt8: return ReadString<uint8_t>();
				case TypeRef::Int16: return ReadString<int16_t>();
				case TypeRef::UInt16: return ReadString<uint16_t>();
				case TypeRef::Int32: return ReadString<int32_t>();
				case TypeRef::UInt32: return ReadString<uint32_t>();
				case TypeRef::Int64: return ReadString<int64_t>();
				case TypeRef::UInt64: return ReadString<uint64_t>();
				case TypeRef::Bool: return Read<bool>() ? "true" : "false";
				case TypeRef::Float: return ReadString<float>();
				case TypeRef::Double: return ReadString<double>();
				default:
					Error("Bad map key {}", GetString(ref));
				}
				std::unreachable();
			};

			if (map.size) {
				for (uint32_t i = 0; i < map.size; ++i) {
					if (map.key.IsBuiltin()) {
						const auto keyString = GetKeyString(map.key);
						auto& pairValue = dataValue.emplace_back();
						pairValue["Type"] = "StdMapType::Pair";
						auto& pairData = pairValue["Data"];
						pairData["Key"] = keyString;
						auto& pairDataValue = pairData["Value"];
						ReadType(pairDataValue, map.value, isDiff);
					}
					else {
						if (_stricmp(elementTypeName, "BSResource::ID") == 0) {
							BSResource::ID id;
							*this >> id.file >> id.ext >> id.dir;
							std::string key = GetFormatedResourceId(id);
							auto& mapValue = value[std::move(key)];
							ReadType(mapValue, map.value, isDiff);
						}
						else {
							Error("Bad map key {}", GetString(map.key));
						}
					}
				}
			}
			else {
				dataValue = nlohmann::json::array();
			}
		}

		void ReadChunk(nlohmann::json& value) {
			Chunk chunk = Read<Chunk>();
			switch (chunk.sig) {
			case Chunk::OBJT:
			case Chunk::DIFF:
			{
				TypeRef ref;
				*this >> ref;
				//value["Type"] = GetString(ref);
				//auto& dataValue = value["Data"];
				//ReadType(dataValue, ref, chunk.IsDiff());
				ReadType(value, ref, chunk.IsDiff());
				//_rootSize++;
				break;
			}
			case Chunk::USER:
			case Chunk::USRD:
			{
				if (userQueue.empty())
					Error("No user cast found {}", chunk.GetSig());
				User user;
				*this >> user;

				//auto&& cast = std::move(userQueue.front());
				//userQueue.pop();
				auto&& cast = std::move(userQueue.back());
				userQueue.pop_back();

				ReadType(cast.value, user.casted, chunk.IsDiff(), true);
				*this >> userValue;
				//if (userValue != 0) {
				//	Log("User value was not 0, {}, pos {:08X}", userValue, Pos());
				//}
				break;
			}
			case Chunk::LIST:
			case Chunk::MAPC:
			{
				if (chunkQueue.empty())
					Error("No chunk found for {}", chunk.GetSig());

				//auto&& nextChunk = std::move(chunkQueue.front());
				//chunkQueue.pop();
				auto&& nextChunk = std::move(chunkQueue.back());
				chunkQueue.pop_back();

				if (chunk.IsList())
					ReadList(nextChunk.value, nextChunk.isDiff);
				else
					ReadMap(nextChunk.value, nextChunk.isDiff);

				break;
			}
			default:
				Error("Uknown chunk type {}", chunk.GetSig());
			}
		}

		void ReadType(nlohmann::json& value, const TypeRef ref, bool isDiff, bool isCast = false)
		{
			switch (ref.data) {
			case TypeRef::Null: {
				value = nullptr;
				break;
			}
			case TypeRef::String: value = ReadString<std::string>(); break;
			case TypeRef::List:
			case TypeRef::Map:
			{
				Error("Bad value type ref: {:08X}", ref.data);
				break;
			}
			case TypeRef::Ref:
			{
				TypeRef ref;
				*this >> ref;
				if (ref.IsBuiltin()) {
					if (ref == TypeRef::Null) {
						//dataValue["Data"] = nullptr;
						//dataValue["Type"] = "<missing_type>";
						value = nullptr;
					}
					else {
						Error("Failed to ref builtin type {}", GetString(ref));
					}
				}
				else {
					auto& type = GetType(ref);
					if (!type) {
						Error("No ref type found {:08X}", ref.data);
					}
					auto& dataValue = value["Data"];
					value["Type"] = "<ref>";
					if (type.IsUser()) {
						//userQueue.emplace(dataValue, ref);
						userQueue.emplace_back(dataValue, ref);
					}
					else {
						ReadType(dataValue, ref, isDiff);
					}
				}
				break;
			}
			case TypeRef::Int8: value = ReadString<int8_t>(); break;
			case TypeRef::UInt8: value = ReadString<uint8_t>(); break;
			case TypeRef::Int16: value = ReadString<int16_t>(); break;
			case TypeRef::UInt16: value = ReadString<uint16_t>(); break;
			case TypeRef::Int32: value = ReadString<int32_t>(); break;
			case TypeRef::UInt32: value = ReadString<uint32_t>(); break;
			case TypeRef::Int64: value = ReadString<int64_t>(); break;
			case TypeRef::UInt64: value = ReadString<uint64_t>(); break;
			case TypeRef::Bool: value = Read<bool>() ? "true" : "false"; break;
			//case TypeRef::Bool: value = Read<bool>() ? true : false; break;
			case TypeRef::Float: value = ReadString<float>(); break;
			case TypeRef::Double: value = ReadString<double>(); break;
			default:
			{
				const char* typeName = GetString(StringRef{ ref.data });
				if (_stricmp("BSComponentDB2::ID", typeName) == 0) {
					uint32_t id = 0;
					if (!isDiff) {
						id = Read();
					}
					else {
						auto fieldPadBegin = Read<uint16_t>();
						id = Read();
						auto fieldPadEnd = Read<uint16_t>();
					}
					value = id != 0 ? std::to_string(id) : "";
				}
				else {
					auto& type = GetType(ref);
					if (!type)
						Error("Type not found: {:08X}", ref.data);
					if (!isCast && type.IsUser()) {
						//userQueue.emplace(value, ref);
						userQueue.emplace_back(value, ref);
					}
					else {
						//auto& classValue = value[GetString(type.name)];
						value["Type"] = GetString(type.name);
						auto& dataValue = value["Data"];
						dataValue = nlohmann::json::object();

						if (!isDiff) {
							for (auto& field : type.fields) {
								//auto& fieldValue = value[GetString(field.name)];
								auto& fieldValue = dataValue[GetString(field.name)];
								if (field.typeId.IsChunk()) {
									//chunkQueue.emplace(fieldValue, isDiff);
									chunkQueue.emplace_back(fieldValue, isDiff);
								}
								else {
									ReadType(fieldValue, field.typeId, isDiff);
								}
							}
						}
						else {
							uint16_t fieldIdx = Read<uint16_t>();
							while (fieldIdx != 0xFFFFu) {
								auto& field = type.fields.at(fieldIdx);
								//auto& fieldValue = value[GetString(field.name)];
								auto& fieldValue = dataValue[GetString(field.name)];
								if (field.typeId.IsChunk()) {
									//chunkQueue.emplace(fieldValue, isDiff);
									chunkQueue.emplace_back(fieldValue, isDiff);
								}
								else {
									ReadType(fieldValue, field.typeId, isDiff);
								}
								*this >> fieldIdx;
							}
						}
						
						//for (auto&& chunk : newChunks) {
						//	chunkQueue.emplace(std::move(chunk));
						//}
					}
				}
			}
			}
		}

		void GetJsonChunkCount(const nlohmann::json& value, uint32_t& count) {
			if (value.is_object()) {
				const std::string& typeName = value["Type"];
				if (typeName == "<collection>") {
					//map
					count++;
				}
				else {
					auto& type = GetType(typeName.c_str());
					if (type.IsUser()) {
						//user
						count++;
					}
				}
				for (auto& member : value["Data"]) {
					GetJsonChunkCount(member, count);
				}
			}
			else if (value.is_array()) {
				//list
				count++;
				for (uint32_t i = 0; i < value.size(); ++i) {
					GetJsonChunkCount(value[i], count);
				}
			}
		}

	//	void GetDefaultType(nlohmann::json& value, const TypeRef ref) {
	//		switch (ref.data) {
	//		case TypeRef::Null: 
	//			value = nullptr;
	//			break;
	//		case TypeRef::String: 
	//			value = ""; 
	//			break;
	//		case TypeRef::Int8: 
	//		case TypeRef::UInt8:
	//		case TypeRef::Int16: 
	//		case TypeRef::UInt16:
	//		case TypeRef::Int32: 
	//		case TypeRef::UInt32:
	//		case TypeRef::Int64: 
	//		case TypeRef::UInt64: 
	//			value = "0";
	//			break;
	//		case TypeRef::Bool: 
	//			value = "false";
	//			break;
	//		case TypeRef::Float:
	//		case TypeRef::Double:
	//			value = "0.0";
	//			break;
	//		case TypeRef::List: 
	//			value["Type"] = "<collection>";
	//			value["Data"] = nlohmann::json::array();
	//			break;
	//		case TypeRef::Map: 
	//			value["Type"] = "<collection>";
	//			value["Data"] = nlohmann::json::object();
	//			break;
	//		case TypeRef::Ref:
	//		{
	//			value["Type"] = "<ref>";
	//			value["Data"] = nullptr;
	//			break;
	//		}
	//		default:
	//		{
	//			const char* typeName = GetString(StringRef{ ref.data });
	//			if (_stricmp(typeName, "BSComponentDB2::ID") == 0) {
	//				value = "";
	//			}
	//			else {
	//				auto& type = GetType(ref);
	//				if (!type)
	//					Error("Unkown type {}", typeName);
	//				value["Type"] = typeName;
	//				auto& dataValue = value["Data"];
	//				for (auto& field : type.fields) {
	//					const char* fieldName = GetString(field.name);
	//					auto& fieldValue = dataValue[fieldName];
	//					GetDefaultType(fieldValue, { field.typeId });
	//				}
	//			}
	//		}
	//		}
	//	}

	};

	class Buffer {
	private:
		std::vector<char> buf;

	public:
		inline uint32_t Size() const { return (uint32_t)buf.size(); }
		inline const char* Data() const { return buf.data(); }
		inline void Clear() { buf.clear(); }

		template <class T = uint32_t>
		Buffer& operator<<(const T& rhs) {
			for (int i = 0; i < sizeof(T); ++i)
				buf.emplace_back((std::bit_cast<const char*>(&rhs)[i]));
			return *this;
		}

		Buffer& operator<<(const std::string& str) {
			*this << (uint16_t)(str.size() + 1);
			buf.insert(buf.end(), str.begin(), str.end());
			buf.emplace_back('\0');
			return *this;
		}
	};

	class Writer {
	public:
		struct Header {
			uint32_t version;
			uint32_t chunkSize;
			const std::vector<char>& stringTable;
			const std::vector<Class>& classes;
		};

		struct QueuedChunk {
			const nlohmann::json& value;
			bool isDiff;
		};

		struct QueuedCast {
			const nlohmann::json& value;
			TypeRef type;
		};

	private:
		std::ostream& out;
		const Header& header;
		std::vector<char> buffer;
		std::queue<QueuedChunk> chunkQueue;
		std::queue<QueuedCast> userQueue;

	public:
		Writer(std::ostream& _out, const Header& _header) : out(_out), header(_header) {}

		template <class T = uint32_t>
		Writer& operator<<(const T& rhs) {
			out.write(reinterpret_cast<const char*>(&rhs), sizeof(T));
			return *this;
		}

		template <>
		Writer& operator<<<std::string>(const std::string& rhs) {
			uint16_t size = (uint16_t)rhs.size() + 1;
			out.write(reinterpret_cast<const char*>(&size), 2);
			out.write(rhs.data(), size);
			return *this;
		}

		template <class T, class U>
		Writer& operator<<(const std::pair<T, U>& rhs) {
			return *this << rhs.first << rhs.second;
		}

		template <class T>
		Writer& operator<<(const std::vector<T>& rhs) {
			//using value_t = std::vector<T>::value_type;
			//*this << Chunk{ 'TSIL', 0x8u + (uint32_t)sizeof(T) * (uint32_t)rhs.size() };
			for (auto& element : rhs) {
				*this << element;
			}
			return *this;
		}

		Writer& operator<<(const Buffer& buf) {
			out.write(buf.Data(), buf.Size());
			return *this;
		}

		template <class T, class U>
		Writer& operator<<(const std::unordered_map<T, U>& rhs) {
			//using value_t = std::unordered_map<T, U>::value_type;
			//*this << Chunk{ 'CPAM', 0xCu + (uint32_t)sizeof(value_t) * (uint32_t)rhs.size() };
			for (auto& [key, value] : rhs) {
				*this << key << value;
			}
			return *this;
		}

		Writer& operator<<(const BSResource::ID& rhs) {
			return *this << rhs.file << rhs.ext << rhs.dir;
		}

		Writer& operator<<(const BSComponentDB2::ID& rhs) {
			return *this << rhs.Value;
		}

		Writer& operator<<(const BSComponentDB2::DBFileIndex::ObjectInfo& rhs) {
			return *this << rhs.PersistentID << rhs.DBID << rhs.Parent << rhs.HasData;
		}

		Writer& operator<<(const BSComponentDB2::DBFileIndex::EdgeInfo& rhs) {
			return *this << rhs.SourceID << rhs.TargetID << rhs.Index << rhs.Type;
		}

		const char* GetString(StringRef ref) const {
			return header.stringTable.data() + ref.data;
		}

		constexpr static uint32_t npos = 0xFFFFFFFFu;

		const Class emptyClass{};
		const Class& GetType(const char* typeName) {
			for (auto& type : header.classes) {
				if (_stricmp(GetString(type.name), typeName) == 0)
					return type;
			}
			return emptyClass;
		}

		const Class& GetType(TypeRef offset) {
			for (auto& type : header.classes) {
				if (type.name.data == offset.data)
					return type;
			}
			return emptyClass;
		}

		const TypeRef GetBuilinType(const char* typeName) {
			for (uint32_t i = 0; i < TypeRef::testStrings.size(); ++i) {
				uint32_t idx = TypeRef::testStrings[i];
				if (_stricmp(TypeRef::builtinStrings[idx], typeName) == 0)
					return { 0xFFFFFF00 | idx };
			}
			return { TypeRef::Npos };
		}

		uint32_t GetTypeOffset(const char* typeName) {
			for (auto& type : header.classes) {
				if (strcmp(GetString(type.name), typeName) == 0)
					return type.name.data;
			}
			return npos;
		}

		Class::Field emptyField{ 0, 0, Class::Null, 0 };
		const Class::Field& GetField(const Class& type, const std::string& fieldName) {
			for (auto& field : type.fields) {
				if (_stricmp(GetString(field.name), fieldName.c_str()) == 0)
					return field;
			}
			return emptyField;
		};

		void WriteHeader() {
			*this << Chunk{ 'HTEB',  8 } << header.version << header.chunkSize
				<< Chunk{ 'TRTS', (uint32_t)header.stringTable.size() };
			out.write(header.stringTable.data(), header.stringTable.size());

			*this << Chunk{ 'EPYT', 4 } << (uint32_t)header.classes.size();
			for (auto& type : header.classes) {
				*this << Chunk{ 'SALC', 0xCu + type.fieldSize * 0xCu };
				*this << type.name << type.typeId << type.flags << type.fieldSize;
				for (auto& field : type.fields) {
					*this << field.name << field.typeId << field.offset << field.size;
				}
			}
		}

		struct UpdateInfo {

		};

		struct CreateInfo {
			nlohmann::json json;
			BSResource::ID id;
			uint64_t hash;
		};

		void WriteDatabase(const Manager& manager, const std::vector<CreateInfo>& creates) {
			*this << Chunk{ 'TJBO', 0x7u + (uint32_t)manager.database.BuildVersion.size() }
				<< GetTypeOffset("BSMaterial::Internal::CompiledDB") << manager.database.BuildVersion;

			const uint32_t hashmapSize = (uint32_t)manager.database.HashMap.size() + (uint32_t)creates.size();

			*this << Chunk{ 'CPAM', 0xCu + 0x14u * hashmapSize }
				<< GetTypeOffset("BSResource::ID") << TypeRef::UInt64 << hashmapSize
				<< manager.database.HashMap;

			for (auto& info : creates) {
				*this << info.id << info.hash;
			}

			*this << Chunk{ 'TSIL', 0x8u } << TypeRef::Null << 0u
				<< Chunk{ 'TSIL', 0x8u } << TypeRef::Null << 0u
				<< Chunk{ 'TJBO', 0x5u } << GetTypeOffset("BSComponentDB2::DBFileIndex") << manager.fileIndex.Optimized
				<< Chunk{ 'CPAM', 0xCu + 0x5u * (uint32_t)manager.fileIndex.ComponentTypes.size() }
				<< TypeRef::UInt16 << GetTypeOffset("BSComponentDB2::DBFileIndex::ComponentTypeInfo") 
				<< (uint32_t)manager.fileIndex.ComponentTypes.size();

			for (auto& [key, value] : manager.fileIndex.ComponentTypes) {
				*this << key << value.Version << value.IsEmpty;
			}
			auto classReferenceType = GetTypeOffset("ClassReference");
			for (auto& [key, value] : manager.fileIndex.ComponentTypes) {
				*this << Chunk{ 'RESU', 0xF + (uint32_t)value.Class.size() }
					<< classReferenceType << TypeRef::String << value.Class << 0u;
			}

			uint32_t objectsSize = (uint32_t)manager.fileIndex.Objects.size();
			for (auto& info : creates) {
				objectsSize += (uint32_t)info.json["Objects"].size();
			}

			*this << Chunk{ 'TSIL', 0x8u + 0x15 * objectsSize }
				<< GetTypeOffset("BSComponentDB2::DBFileIndex::ObjectInfo") << objectsSize
				<< manager.fileIndex.Objects;

			//uint32_t objectId = manager.nextObjectId;
			for (auto& info : creates) {
				auto& objects = info.json["Objects"];
				for (size_t i = 0; i < objects.size(); ++i) {
					const auto& object = objects[i];
					const std::string& idString = object["ID"];
					uint32_t objectId = std::stoul(idString);
					auto resourceId = i == 0 ? info.id : GetResourceIdFromPath(idString);
					*this << BSComponentDB2::DBFileIndex::ObjectInfo{ resourceId, objectId, 0, false };
				}
			}

			//uint32_t componentsSize = (uint32_t)manager.fileIndex.Components.size();
			uint32_t componentsSize = 0;
			for (auto& info : creates) {
				auto& objects = info.json["Objects"];
				for (auto& object : objects) {
					componentsSize += (uint32_t)object["Components"].size();
				}
			}
			*this << Chunk{ 'TSIL', 0x8u + 0x8u * componentsSize }
				<< GetTypeOffset("BSComponentDB2::DBFileIndex::ComponentInfo") << componentsSize
				<< manager.fileIndex.Components;

			//objectId = manager.nextObjectId;
			for (auto& info : creates) {
				auto& objects = info.json["Objects"];
				for (auto& object : objects) {
					const std::string& idString = object["ID"];
					uint32_t objectId = std::stoul(idString);
					auto& components = object["Components"];
					for (auto& component : components) {
						uint16_t index = component["Index"];
						const std::string& typeName = component["Type"];
						uint16_t type = manager.GetTypeIndex(typeName.c_str());
						*this << BSComponentDB2::DBFileIndex::ComponentInfo{ objectId, index, type };
					}
					//objectId++;
				}
			}

			*this << Chunk{ 'TSIL', 0x8u + 0xCu * (uint32_t)manager.fileIndex.Edges.size() }
				<< GetTypeOffset("BSComponentDB2::DBFileIndex::EdgeInfo") << (uint32_t)manager.fileIndex.Edges.size()
				<< manager.fileIndex.Edges;
		}

		void WriteTestDatabase(const Manager& manager, const TestStruct& tester) {
			*this << Chunk{ 'TJBO', 0x7u + (uint32_t)manager.database.BuildVersion.size() }
			<< GetTypeOffset("BSMaterial::Internal::CompiledDB") << manager.database.BuildVersion;

			const uint32_t hashmapSize = 1;
			*this << Chunk{ 'CPAM', 0xCu + 0x14u * hashmapSize }
				<< GetTypeOffset("BSResource::ID") << TypeRef::UInt64 << hashmapSize;
				//<< manager.database.HashMap;

			//for (auto& info : creates) {
			//	*this << info.id << info.hash;
			//}

			*this << tester.resourceId << tester.hash;

			*this << Chunk{ 'TSIL', 0x8u } << TypeRef::Null << 0u
				<< Chunk{ 'TSIL', 0x8u } << TypeRef::Null << 0u
				<< Chunk{ 'TJBO', 0x5u } << GetTypeOffset("BSComponentDB2::DBFileIndex") << manager.fileIndex.Optimized
				<< Chunk{ 'CPAM', 0xCu + 0x5u * (uint32_t)manager.fileIndex.ComponentTypes.size() }
				<< TypeRef::UInt16 << GetTypeOffset("BSComponentDB2::DBFileIndex::ComponentTypeInfo")
				<< (uint32_t)manager.fileIndex.ComponentTypes.size();

			for (auto& [key, value] : manager.fileIndex.ComponentTypes) {
				*this << key << value.Version << value.IsEmpty;
			}
			auto classReferenceType = GetTypeOffset("ClassReference");
			for (auto& [key, value] : manager.fileIndex.ComponentTypes) {
				*this << Chunk{ 'RESU', 0xF + (uint32_t)value.Class.size() }
				<< classReferenceType << TypeRef::String << value.Class << 0u;
			}

			//uint32_t objectsSize = (uint32_t)manager.fileIndex.Objects.size();
			uint32_t objectsSize = 0;
			//for (auto& info : creates) {
			//	objectsSize += (uint32_t)info.json["Objects"].size();
			//}
			objectsSize += (uint32_t)tester.objectSet.size();

			*this << Chunk{ 'TSIL', 0x8u + 0x15 * objectsSize }
				<< GetTypeOffset("BSComponentDB2::DBFileIndex::ObjectInfo") << objectsSize;
				//<< manager.fileIndex.Objects;

			//uint32_t objectId = manager.nextObjectId;
			//for (auto& info : creates) {
			//	auto& objects = info.json["Objects"];
			//	for (size_t i = 0; i < objects.size(); ++i) {
			//		const auto& object = objects[i];
			//		const std::string& idString = object["ID"];
			//		uint32_t objectId = std::stoul(idString);
			//		auto resourceId = i == 0 ? info.id : GetResourceIdFromPath(idString);
			//		*this << BSComponentDB2::DBFileIndex::ObjectInfo{ resourceId, objectId, 0, false };
			//	}
			//}

			for (auto& objectId : tester.objectSet) {
				//*this << manager.fileIndex.Objects.at(objectId);
				*this << manager.GetObject({ objectId });
			}

			//uint32_t componentsSize = (uint32_t)manager.fileIndex.Components.size();
			//uint32_t componentsSize = 0;
			//for (auto& info : creates) {
			//	auto& objects = info.json["Objects"];
			//	for (auto& object : objects) {
			//		componentsSize += (uint32_t)object["Components"].size();
			//	}
			//}
			uint32_t componentsSize = (uint32_t)tester.componentSet.size();
			*this << Chunk{ 'TSIL', 0x8u + 0x8u * componentsSize }
				<< GetTypeOffset("BSComponentDB2::DBFileIndex::ComponentInfo") << componentsSize;
				//<< manager.fileIndex.Components;

			//objectId = manager.nextObjectId;
			//for (auto& info : creates) {
			//	auto& objects = info.json["Objects"];
			//	for (auto& object : objects) {
			//		const std::string& idString = object["ID"];
			//		uint32_t objectId = std::stoul(idString);
			//		auto& components = object["Components"];
			//		for (auto& component : components) {
			//			uint16_t index = component["Index"];
			//			const std::string& typeName = component["Type"];
			//			uint16_t type = manager.GetTypeIndex(typeName.c_str());
			//			*this << BSComponentDB2::DBFileIndex::ComponentInfo{ objectId, index, type };
			//		}
			//		//objectId++;
			//	}
			//}
			for (auto& componentIdx : tester.componentSet) {
				*this << manager.fileIndex.Components.at(componentIdx);
			}

			uint32_t edgesSize = 0;
			edgesSize += (uint32_t)tester.edgeSet.size();
			*this << Chunk{ 'TSIL', 0x8u + 0xCu * edgesSize }
				<< GetTypeOffset("BSComponentDB2::DBFileIndex::EdgeInfo") << edgesSize;
				//<< manager.fileIndex.Edges;
			for (auto& edgeIdx : tester.edgeSet) {
				*this << manager.fileIndex.Edges.at(edgeIdx);
			}
		}

		void WriteChunk(Reader& reader) {
			auto chunk = reader.Read<Chunk>();
			*this << chunk;
			buffer.resize(chunk.size);
			auto& inStream = reader.Stream();
			inStream.read(buffer.data(), buffer.size());
			out.write(buffer.data(), buffer.size());
			char peek = inStream.peek();
			while (reader.ChunksRemaining() && peek != 'O' && peek != 'D') {
				chunk = reader.Read<Chunk>();
				*this << chunk;
				buffer.resize(chunk.size);
				auto& inStream = reader.Stream();
				inStream.read(buffer.data(), buffer.size());
				out.write(buffer.data(), buffer.size());
				peek = inStream.peek();
			}
		}

		void WriteComponentJson(const nlohmann::json& json) {
			const std::string& typeName = json["Type"];
			const auto& type = GetType(typeName.c_str());
			if (!type)
				Error("Failed to get component type");

			Buffer buf;
			WriteObject(json, type, buf);
			*this << Chunk{ 'TJBO', buf.Size() + 0x4u } << type.name.data << buf;

			while (chunkQueue.size()) {
				auto&& chunk = std::move(chunkQueue.front());
				chunkQueue.pop();
				buf.Clear();
				if (chunk.value.is_object()) {
					WriteMap(chunk.value, buf);
				}
				else if (chunk.value.is_array()) {
					WriteList(chunk.value, buf);
				}
			}
			while (userQueue.size()) {
				auto&& cast = std::move(userQueue.front());
				userQueue.pop();

				const std::string& castName = cast.value["Type"];
				const auto& castType = GetType(castName.c_str());
				if (!castType)
					Error("Failed to get component type");

				buf.Clear();
				WriteObject(json, castType, buf);
				//TODO get base type to cast from
				//TODO get unk Indentation? value
				*this << Chunk{ 'RESU', buf.Size() } << cast.type << cast.type << buf << 0u;
			}
		}

		void WriteObject(const nlohmann::json& json, const Class& type, Buffer& buf) {
			//const std::string& typeName = json["Type"];
			//const auto& type = GetType(typeName.c_str());
			//if (!type)
			//	Error("Failed to get object type");
			
			//for (auto dataIt = data.begin(); dataIt != data.end(); ++dataIt) {
			//	const auto& field = GetField(type, dataIt.key());
			//	WriteType(*dataIt, field.typeId, buf);
			//}

			if (!json.is_null()) {
				auto& data = json["Data"];
				for (auto& field : type.fields) {
					auto& fieldValue = data[GetString(field.name)];
					WriteType(fieldValue, field.typeId, buf);
				}
			}
			else {
				for (auto& field : type.fields) {
					WriteType(nullptr, field.typeId, buf);
				}
			}
		}

		TypeRef GetElementType(const nlohmann::json& json) {
			TypeRef result;
			const std::string& elementTypeName = json["ElementType"];
			const auto& elementType = GetType(elementTypeName.c_str());
			if (elementType) {
				result = { elementType.name.data };
			}
			else {
				result = GetBuilinType(elementTypeName.c_str());
				if (!result)
					Error("Failed to find element type {}", elementTypeName);
			}
			return result;
		}

		void WriteList(const nlohmann::json& json, Buffer& buf) {
			const auto& data = json["Data"];
			if (data.size() == 0) {
				*this << Chunk{ 'TSIL', 0x8u } << TypeRef::Null << 0u;
				return;
			}

			const std::string& typeName = json["Type"];
			if (_stricmp(typeName.c_str(), "<collection>") != 0)
				Error("List was not a <collection>");

			TypeRef typeRef = GetElementType(json);
			if (!typeRef)
				Error("Failed to find element type");

			for (uint32_t i = 0; i < data.size(); ++i) {
				WriteType(data[i], typeRef, buf);
			}
			*this << Chunk{ 'TSIL', buf.Size() + 0x8u } << typeRef << data.size() << buf;
		}

		void WriteMap(const nlohmann::json& json, Buffer& buf) {
			auto& data = json["Data"];
			if (data.size() == 0) {
				*this << Chunk{ 'CPAM', 0xCu } << TypeRef::Null << TypeRef::Null << 0u;
				return;
			}
			const std::string& typeName = json["Type"];
			if (_stricmp(typeName.c_str(), "<collection>") != 0)
				Error("List was not a <collection>");
			//TypeRef typeRef = GetElementType(json);
			//if (!typeRef)
			//	Error("Failed to find element type");
			for (auto it = data.begin(); it != data.end(); ++it) {
				buf << it.key();
				WriteType(*it, { TypeRef::Ref }, buf);
			}
			*this << Chunk{ 'CPAM', buf.Size() + 0xCu } << TypeRef::String << TypeRef::Ref << data.size() << buf;
		}

		void WriteType(const nlohmann::json& json, const TypeRef ref, Buffer& buf) {
			if (!json.is_null()) {
				switch (ref.data) {
				case TypeRef::Null: buf << 0u; break;
				case TypeRef::String: { const std::string& str = json; buf << str; break; }
				case TypeRef::Int8: { const std::string& str = json; buf << (int8_t)std::stol(str); break; }
				case TypeRef::UInt8: { const std::string& str = json; buf << (uint8_t)std::stoul(str); break; }
				case TypeRef::Int16: { const std::string& str = json; buf << (int16_t)std::stol(str); break; }
				case TypeRef::UInt16: { const std::string& str = json; buf << (uint16_t)std::stoul(str); break; }
				case TypeRef::Int32: { const std::string& str = json; buf << (int32_t)std::stol(str); break; }
				case TypeRef::UInt32: { const std::string& str = json; buf << (uint32_t)std::stoul(str); break; }
				case TypeRef::Int64: { const std::string& str = json; buf << (int64_t)std::stoll(str); break; }
				case TypeRef::UInt64: { const std::string& str = json; buf << (uint64_t)std::stoull(str); break; }
				//case TypeRef::Bool: buf << json.get<bool>(); break;
				case TypeRef::Bool: { const std::string& str = json; buf << (_stricmp(str.c_str(), "true") == 0); break; }
				case TypeRef::Float: { const std::string& str = json; buf << (float)std::stof(str); break; }
				case TypeRef::Double: { const std::string& str = json; buf << (double)std::stod(str); break; }
				case TypeRef::List:
				case TypeRef::Map:
				{
					chunkQueue.push({ json, false });
					break;
				}
				case TypeRef::Ref:
				{
					const std::string& typeName = json["Type"];
					auto& dataValue = json["Data"];
					if (typeName == "<ref>") {
						userQueue.emplace(dataValue, ref);
					}
					else {
						const auto& refType = GetType(typeName.c_str());
						WriteType(dataValue, { refType.name.data }, buf);
					}
					break;
				}
				default:
				{
					//const std::string& typeName = json["Type"];
					auto& type = GetType(ref);
					const char* typeName = GetString({ type.name.data });
					//WriteObject(json, type, buf);
					if (_stricmp(typeName, "BSComponentDB2::ID") == 0) {
						//const std::string& id = json["Data"]["ID"];
						const std::string& id = json;
						if (id.size())
							buf << (uint32_t)std::stoul(id);
						else
							buf << 0u;
					}
					else {
						WriteObject(json, type, buf);
					}
				}
				}
			}
			else {
				switch (ref.data) {
				case TypeRef::Null: buf << 0u; break;
				case TypeRef::String: buf << (uint16_t)0; break;
				case TypeRef::Int8: buf << (int8_t)0; break;
				case TypeRef::UInt8: buf << (uint8_t)0; break;
				case TypeRef::Int16: buf << (int16_t)0; break;
				case TypeRef::UInt16: buf << (uint16_t)0; break;
				case TypeRef::Int32: buf << 0; break;
				case TypeRef::UInt32: buf << 0; break;
				case TypeRef::Int64: buf << (int64_t)0; break;
				case TypeRef::UInt64: buf << (uint64_t)0; break;
				case TypeRef::Bool: buf << false; break;
				case TypeRef::Float: buf << 0.0f; break; 
				case TypeRef::Double: buf << 0.0; break;
				case TypeRef::List:
				case TypeRef::Map:
				{
					chunkQueue.push({ json, false });
					break;
				}
				case TypeRef::Ref:
				{
					//const std::string& typeName = json["Type"];s
					//auto& dataValue = json["Data"];
					//if (typeName == "<ref>") {
					//	userQueue.emplace(dataValue, ref);
					//}
					//else {
					//const auto& refType = GetType(typeRef);
					//WriteType(dataValue, { refType.name.data }, buf);
					//}
					buf << 0u;
					break;
				}
				default:
				{
					//const std::string& typeName = json["Type"];
					auto& type = GetType(ref);
					const char* typeName = GetString({ type.name.data });
					//WriteObject(json, type, buf);
					if (_stricmp(typeName, "BSComponentDB2::ID") == 0) {
						//const std::string& id = json;
						//buf << (uint32_t)std::stoul(id);
						buf << 0u;
					}
					else {
						WriteObject(json, type, buf);
					}
				}
				}
			}
		}
	};
}