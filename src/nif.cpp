#include "nif.h"

#include <nlohmann/json.hpp>

#include "util.h"

#include "nifly/include/NifFile.hpp"
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

bool AddSkeletonBones(const std::filesystem::path& inSkeleton, const std::filesystem::path& dstSkeleton, const std::filesystem::path& boneJsonPath) {
	nifly::NifFile nif;
	if (nif.Load(inSkeleton) != 0) {
		Log("Failed to load skeleton {}", inSkeleton.string());
		return false;
	}

	nlohmann::json json;
	{
		std::ifstream in(boneJsonPath, std::ios::in | std::ios::binary);
		if (in.fail()) {
			Log("Failed to load bone json {}", boneJsonPath.string());
			return false;
		}
		in >> json;
	}

	auto& bones = json["bones"];
	if (bones.is_null()) {
		Log("Failed to find \"bones\" in json {}", boneJsonPath.string());
		return false;
	}

	const auto& MatrixFromAxisAngle = [](const Vector3& axis, float theta) -> Matrix3 {
		float c = std::cosf(theta);
		float s = std::sinf(theta);
		float t = 1.0f - c;
		return {
			t * axis.x * axis.x + c, t * axis.x * axis.y - axis.z * s, t * axis.x * axis.z + axis.y * s,
			t * axis.x * axis.y + axis.z * s, t * axis.y * axis.y + c, t * axis.y * axis.z - axis.x * s,
			t * axis.x * axis.z - axis.y * s, t * axis.y * axis.z + axis.x * s, t * axis.z * axis.z + c
		};
	};

	const auto& RotateTowards = [&MatrixFromAxisAngle](MatTransform& transform, const Vector3& target) {
		Vector3 axis = transform.rotation * Vector3{ 1.0f, 0.0f, 0.0f };
		axis.Normalize();
		Vector3 goal = target - transform.translation;
		goal.Normalize();
		Vector3 cross = axis.cross(goal);
		cross.Normalize();
		if (!cross.IsZero(true)) {
			float theta = std::acosf(axis.dot(goal) / (axis.length() * goal.length()));
			if (!std::isnan(theta) && !FloatsAreNearlyEqual(theta, 0.0f)) {
				const auto axisAngle = MatrixFromAxisAngle(cross, theta);
				//transform.rotation = transform.rotation * axisAngle;
				transform.rotation = axisAngle * transform.rotation;
			}
		}
	};

	Log("Adding bones to skeleton {}", inSkeleton.string());

	for (auto& bone : json["bones"]) {
		const std::string& nameStr = bone["name"];
		const std::string& parentStr = bone["parent"];
		const auto& head = bone["head"];
		const auto& tail = bone["tail"];
		const auto& roll = bone["roll"];
		const Vector3 headPos{ head[0], head[1], head[2] };
		const Vector3 tailPos{ tail[0], tail[1], tail[2] };
		NiNode* parent = nif.FindBlockByName<NiNode>(parentStr);
		if (parent) {
			MatTransform parentWorld;
			nif.GetNodeTransformToGlobal(parentStr, parentWorld);
			MatTransform nodeWorld;
			nodeWorld.translation = headPos;
			//nodeWorld.rotation = parentWorld.rotation;
			constexpr float toRadian = PI / 180.0f;
			nodeWorld.rotation.MakeRotation(roll * toRadian, 0.0f, 0.0f);
			RotateTowards(nodeWorld, tailPos);
			nodeWorld.scale = parentWorld.scale;
			MatTransform nodeLocal = parentWorld.InverseTransform().ComposeTransforms(nodeWorld);
			//MatTransform nodeLocal = nodeWorld.InverseTransform().ComposeTransforms(parentWorld);
			nif.AddNode(nameStr, nodeLocal, parent);
			Log("Added {}", nameStr);
		}
		else {
			Log("Failed to find parent {} for bone {}", parentStr, nameStr);
		}
	}

	if (nif.Save(dstSkeleton) != 0) {
		Log("Failed to save skeleton {}", dstSkeleton.string());
		return false;
	}

	Log("Saved {}", dstSkeleton.string());
	return true;
}