#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace BSResource {
	struct ID {
		uint32_t dir;
		uint32_t file;
		uint32_t ext;

		bool operator==(const ID& rhs) const {
			return dir == rhs.dir && file == rhs.file && ext == rhs.ext;
		}
	};
}

template<>
struct std::hash<BSResource::ID>
{
	std::size_t operator()(const BSResource::ID& id) const noexcept
	{
		//Assuming ext is the same for each id
		return std::hash<uint64_t>()((uint64_t)id.dir << 32 | id.file);
	}
};

namespace BSMaterial {
	namespace Internal {
		struct CompiledDB {
			struct FilePair {
				BSResource::ID First;
				BSResource::ID Second;
			};

			std::string BuildVersion;
			//std::unordered_map<BSResource::ID, uint64_t> HashMap;
			std::vector<std::pair<BSResource::ID, uint64_t>> HashMap;
			std::vector<FilePair> Collisions;
			std::vector<nullptr_t> Circular;
		};
	}
}

namespace BSComponentDB2 {
	struct ID {
		uint32_t Value;
		bool operator==(const ID& rhs) const {
			return Value == rhs.Value;
		}
	};

	struct DBFileIndex {
		struct ComponentTypeInfo {
			std::string Class;
			uint16_t Version;
			bool IsEmpty;
		};

		struct ObjectInfo {
			BSResource::ID PersistentID;
			ID DBID;
			ID Parent;
			bool HasData;

			operator bool() const { return DBID.Value; }
		};

		struct ComponentInfo {
			ID ObjectID;
			uint16_t Index;
			uint16_t Type;

			operator bool() const { return ObjectID.Value; }
		};
		
		struct EdgeInfo {
			ID SourceID;
			ID TargetID;
			uint16_t Index;
			uint16_t Type;
		};

		//std::unordered_map<uint16_t, ComponentTypeInfo> ComponentTypes;
		std::vector<std::pair<uint16_t, ComponentTypeInfo>> ComponentTypes;
		std::vector<ObjectInfo> Objects;
		std::vector<ComponentInfo> Components;
		std::vector<EdgeInfo> Edges;
		bool Optimized;
	};
}