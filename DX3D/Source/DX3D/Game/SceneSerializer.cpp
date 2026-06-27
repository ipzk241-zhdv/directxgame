#include <DX3D/Game/SceneSerializer.h>
#include <Game/Kepler/OrbitData.h>
#include <DX3D/Core/Logger.h>
#include <fstream>
#include <algorithm>
#include <vector>

namespace dx3d {

	SceneSerializer::SceneSerializer(Registry& registry, TransformSystem& tSys, RenderComponentSystem& rSys, ComponentStorage<NameComponent>& nSys, ComponentStorage<TagComponent>& tagSys, ComponentStorage<ModelComponent>& mSys, OrbitSystem& oSys, SceneManager& sMan, AssetManager& aMan)
		: m_registry(registry), m_orbitSystem(oSys), m_transformSystem(tSys), m_renderSystem(rSys), m_nameSystem(nSys), m_tagSystem(tagSys), m_modelSystem(mSys), m_sceneManager(sMan), m_assetManager(aMan) {
	}

	int SceneSerializer::CalculateSimDepth(Entity entity) const {
		int depth = 0;
		Entity current = entity;

		while (m_orbitSystem.hasOrbit(current)) {
			const auto& orbit = m_orbitSystem.getOrbit(current);
			if (orbit.ParentEntity == Entity::Null) break;
			++depth;
			current = orbit.ParentEntity;
		}
		return depth;
	}

	uint32_t SceneSerializer::GetEntityIdFromOrbitEntity(Entity entity) const {
		return entity == Entity::Null ? static_cast<uint32_t>(-1) : entity.id;
	}

	bool SceneSerializer::Serialize(const std::string& filepath) {
		nlohmann::json sceneRoot;
		sceneRoot["version"] = 2;

		sceneRoot["runtime"] = nlohmann::json::object();
		sceneRoot["runtime"]["entities"] = nlohmann::json::array();

		sceneRoot["editor"] = nlohmann::json::object();
		sceneRoot["editor"]["objects"] = nlohmann::json::array();

		struct NodeWrapper {
			Entity entity;
			int simDepth;
			nlohmann::json jsonPayload;
		};

		std::vector<NodeWrapper> sortingPayload;
		const auto& activeEntities = m_transformSystem.getRawEntities();
		const auto& denseTransforms = m_transformSystem.getRawData();

		for (size_t i = 0; i < activeEntities.size(); ++i) {
			Entity e = activeEntities[i];
			NodeWrapper node;
			node.entity = e;
			node.simDepth = 0;

			node.jsonPayload["fileId"] = e.id;
			const bool hasOrbit = m_orbitSystem.hasOrbit(e);
			node.jsonPayload["hasOrbit"] = hasOrbit;

			const auto& transform = denseTransforms[i];
			Vec3d p = transform.getPosition();
			Vec3d s = transform.getScale();
			node.jsonPayload["transform"] = {
				{"position", {p.x, p.y, p.z}},
				{"rotation", {transform.getQuaternion().x, transform.getQuaternion().y, transform.getQuaternion().z, transform.getQuaternion().w}},
				{"scale", {s.x, s.y, s.z}}
			};

			if (hasOrbit) {
				node.simDepth = CalculateSimDepth(e);
				const Simulator::OrbitData& orbit = m_orbitSystem.getOrbit(e);
				node.jsonPayload["orbit"] = {
					{"attractorMass", orbit.AttractorMass},
					{"bodyMass", orbit.BodyMass},
					{"gravConst", orbit.GravConst},
					{"mg", orbit.MG},
					{"semiMajorAxis", orbit.SemiMajorAxis},
					{"semiMinorAxis", orbit.SemiMinorAxis},
					{"eccentricity", orbit.Eccentricity},
					{"focalParameter", orbit.FocalParameter},
					{"period", orbit.Period},
					{"meanMotion", orbit.MeanMotion},
					{"trueAnomaly", orbit.TrueAnomaly},
					{"meanAnomalyEpoch", orbit.MeanAnomaly},
					{"eccentricAnomaly", orbit.EccentricAnomaly},
					{"centerPoint", {orbit.CenterPoint.x, orbit.CenterPoint.y, orbit.CenterPoint.z}},
					{"orbitNormal", {orbit.OrbitNormal.x, orbit.OrbitNormal.y, orbit.OrbitNormal.z}},
					{"semiMajorAxisBasis", {orbit.SemiMajorAxisBasis.x, orbit.SemiMajorAxisBasis.y, orbit.SemiMajorAxisBasis.z}},
					{"semiMinorAxisBasis", {orbit.SemiMinorAxisBasis.x, orbit.SemiMinorAxisBasis.y, orbit.SemiMinorAxisBasis.z}},
					{"periapsis", {orbit.Periapsis.x, orbit.Periapsis.y, orbit.Periapsis.z}},
					{"apoapsis", {orbit.Apoapsis.x, orbit.Apoapsis.y, orbit.Apoapsis.z}},
					{"periapsisDistance", orbit.PeriapsisDistance},
					{"apoapsisDistance", orbit.ApoapsisDistance},
					{"parentFileId", GetEntityIdFromOrbitEntity(orbit.ParentEntity)},
					{"positionRelativeToAttractor", {orbit.positionRelativeToAttractor.x, orbit.positionRelativeToAttractor.y, orbit.positionRelativeToAttractor.z}},
					{"absoluteWorldPosition", {orbit.absoluteWorldPosition.x, orbit.absoluteWorldPosition.y, orbit.absoluteWorldPosition.z}},
					{"orbitalVelocity", {orbit.velocityRelativeToAttractor.x, orbit.velocityRelativeToAttractor.y, orbit.velocityRelativeToAttractor.z}},
					{"attractorDistance", orbit.AttractorDistance},
					{"orbitCompressionRatio", orbit.OrbitCompressionRatio},
					{"orbitNormalDotEclipticNormal", orbit.OrbitNormalDotEclipticNormal},
					{"sphereOfInfluenceRadius", orbit.SphereOfInfluenceRadius},
					{"isFrozen", orbit.isFrozen},
					{"freezeColor", orbit.freezeColor},
					{"orbitColor", {orbit.orbitColor.x, orbit.orbitColor.y, orbit.orbitColor.z, orbit.orbitColor.w}}
				};
			}
			sortingPayload.push_back(node);
		}

		std::sort(sortingPayload.begin(), sortingPayload.end(), [](const NodeWrapper& a, const NodeWrapper& b) {
			return a.simDepth < b.simDepth;
			});

		for (const auto& node : sortingPayload) {
			sceneRoot["runtime"]["entities"].push_back(node.jsonPayload);
		}

		for (const auto& obj : m_sceneManager.getAllObjects()) {
			nlohmann::json jObj;
			jObj["fileId"] = obj->entity.id;
			jObj["name"] = m_nameSystem.has(obj->entity) ? m_nameSystem.get(obj->entity).name : obj->name;
			jObj["tag"] = m_tagSystem.has(obj->entity) ? m_tagSystem.get(obj->entity).tag : obj->tag;
			jObj["modelName"] = m_modelSystem.has(obj->entity) ? m_modelSystem.get(obj->entity).modelName : obj->modelName;

			Entity parent = Entity::Null;
			bool inheritPosition = true;
			bool inheritRotation = true;
			bool inheritScale = true;
			if (m_transformSystem.hasTransform(obj->entity)) {
				const auto& hierarchy = m_transformSystem.getHierarchy(obj->entity);
				parent = hierarchy.parent;
				inheritPosition = hierarchy.inheritPosition;
				inheritRotation = hierarchy.inheritRotation;
				inheritScale = hierarchy.inheritScale;
			}

			jObj["inheritFlags"] = {
				{"position", inheritPosition},
				{"rotation", inheritRotation},
				{"scale", inheritScale}
			};

			jObj["parentFileId"] = parent.isNull() ? static_cast<uint32_t>(-1) : parent.id;

			sceneRoot["editor"]["objects"].push_back(jObj);
		}

		std::ofstream outFile(filepath);
		if (!outFile.is_open()) return false;
		outFile << sceneRoot.dump(4);
		return true;
	}

	bool SceneSerializer::Deserialize(const std::string& filepath) {
		std::ifstream inFile(filepath);
		if (!inFile.is_open()) return 0;

		nlohmann::json sceneRoot;
		inFile >> sceneRoot;

		int version = sceneRoot.value("version", 1);

		m_registry.clear();
		m_transformSystem.clear();
		m_renderSystem.clear();
		m_nameSystem.clear();
		m_tagSystem.clear();
		m_modelSystem.clear();
		m_orbitSystem.clear();
		m_sceneManager.clear();

		std::unordered_map<uint32_t, Entity> fileToRuntimeMap;
		const nlohmann::json& runtimeEntities = sceneRoot["runtime"]["entities"];

		for (const auto& jEnt : runtimeEntities) {
			Entity liveEntity = m_registry.create();
			uint32_t fileId = jEnt["fileId"];
			fileToRuntimeMap[fileId] = liveEntity;

			Transform t;
			Vec3d tPos{ 0.0, 0.0, 0.0 };
			Vec3d tRot{ 0.0, 0.0, 0.0 };
			double w = 1.0;
			Vec3d tScale{ 1.0, 1.0, 1.0 };
			if (jEnt.contains("transform")) {
				const auto& jTransform = jEnt["transform"];
				if (jTransform.contains("position")) {
					tPos.x = jTransform["position"][0]; tPos.y = jTransform["position"][1]; tPos.z = jTransform["position"][2];
				}
				if (jTransform.contains("rotation")) {
					tRot.x = jTransform["rotation"][0]; tRot.y = jTransform["rotation"][1]; tRot.z = jTransform["rotation"][2]; w = jTransform["rotation"][3];
				}
				if (jTransform.contains("scale")) {
					tScale.x = jTransform["scale"][0]; tScale.y = jTransform["scale"][1]; tScale.z = jTransform["scale"][2];
				}
			}
			t.setPosition(tPos);
			t.setQuaternion(tRot.x, tRot.y, tRot.z, w);
			t.setScale(tScale);
			m_transformSystem.assignTransform(liveEntity, t);
		}

		auto getDouble = [](const nlohmann::json& j, const std::string& key) -> double {
			if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
			return 0.0;
			};
		auto getVec3 = [](const nlohmann::json& j, const std::string& key) -> Vec3d {
			Vec3d v{ 0.0, 0.0, 0.0 };
			if (j.contains(key) && j[key].is_array()) {
				if (j[key][0].is_number()) v.x = j[key][0].get<double>();
				if (j[key][1].is_number()) v.y = j[key][1].get<double>();
				if (j[key][2].is_number()) v.z = j[key][2].get<double>();
			}
			return v;
			};

		for (const auto& jEnt : runtimeEntities) {
			if (!jEnt.value("hasOrbit", false)) continue;

			uint32_t fileId = jEnt["fileId"];
			Entity liveEntity = fileToRuntimeMap[fileId];
			const auto& jOrb = jEnt["orbit"];

			Simulator::OrbitData loadedOrbit;

			// Masses and Constants
			loadedOrbit.AttractorMass = getDouble(jOrb, "attractorMass");
			loadedOrbit.BodyMass = getDouble(jOrb, "bodyMass");
			loadedOrbit.GravConst = getDouble(jOrb, "gravConst");
			loadedOrbit.MG = getDouble(jOrb, "mg");

			// Primary Orbital Elements
			loadedOrbit.SemiMajorAxis = getDouble(jOrb, "semiMajorAxis");
			loadedOrbit.SemiMinorAxis = getDouble(jOrb, "semiMinorAxis");
			loadedOrbit.Eccentricity = getDouble(jOrb, "eccentricity");
			loadedOrbit.FocalParameter = getDouble(jOrb, "focalParameter");

			// Anomalies and Time
			loadedOrbit.Period = getDouble(jOrb, "period");
			loadedOrbit.MeanMotion = getDouble(jOrb, "meanMotion");
			loadedOrbit.TrueAnomaly = getDouble(jOrb, "trueAnomaly");
			loadedOrbit.MeanAnomaly = getDouble(jOrb, "meanAnomalyEpoch");
			loadedOrbit.EccentricAnomaly = getDouble(jOrb, "eccentricAnomaly");

			// Spatial Geometric Bases
			loadedOrbit.CenterPoint = getVec3(jOrb, "centerPoint");
			loadedOrbit.OrbitNormal = getVec3(jOrb, "orbitNormal");
			loadedOrbit.SemiMajorAxisBasis = getVec3(jOrb, "semiMajorAxisBasis");
			loadedOrbit.SemiMinorAxisBasis = getVec3(jOrb, "semiMinorAxisBasis");
			loadedOrbit.Periapsis = getVec3(jOrb, "periapsis");
			loadedOrbit.Apoapsis = getVec3(jOrb, "apoapsis");

			loadedOrbit.PeriapsisDistance = getDouble(jOrb, "periapsisDistance");
			loadedOrbit.ApoapsisDistance = getDouble(jOrb, "apoapsisDistance");

			// State Vectors
			loadedOrbit.positionRelativeToAttractor = getVec3(jOrb, "positionRelativeToAttractor");
			loadedOrbit.absoluteWorldPosition = getVec3(jOrb, "absoluteWorldPosition");
			loadedOrbit.velocityRelativeToAttractor = getVec3(jOrb, "orbitalVelocity");
			loadedOrbit.AttractorDistance = getDouble(jOrb, "attractorDistance");

			// Auxiliary State & Rendering Configuration
			loadedOrbit.OrbitCompressionRatio = getDouble(jOrb, "orbitCompressionRatio");
			loadedOrbit.OrbitNormalDotEclipticNormal = getDouble(jOrb, "orbitNormalDotEclipticNormal");
			loadedOrbit.SphereOfInfluenceRadius = getDouble(jOrb, "sphereOfInfluenceRadius");

			loadedOrbit.isFrozen = jOrb.value("isFrozen", false);
			loadedOrbit.freezeColor = jOrb.value("freezeColor", false);

			if (jOrb.contains("orbitColor") && jOrb["orbitColor"].is_array()) {
				loadedOrbit.orbitColor.x = jOrb["orbitColor"][0].is_number() ? jOrb["orbitColor"][0].get<float>() : 1.0f;
				loadedOrbit.orbitColor.y = jOrb["orbitColor"][1].is_number() ? jOrb["orbitColor"][1].get<float>() : 1.0f;
				loadedOrbit.orbitColor.z = jOrb["orbitColor"][2].is_number() ? jOrb["orbitColor"][2].get<float>() : 1.0f;
				loadedOrbit.orbitColor.w = jOrb["orbitColor"][3].is_number() ? jOrb["orbitColor"][3].get<float>() : 1.0f;
			}

			loadedOrbit.visualDirty = true;

			uint32_t parentFileId = jOrb.value("parentFileId", static_cast<uint32_t>(-1));
			if (parentFileId != static_cast<uint32_t>(-1) && fileToRuntimeMap.count(parentFileId)) {
				loadedOrbit.ParentEntity = fileToRuntimeMap[parentFileId];
			}
			else {
				loadedOrbit.ParentEntity = Entity::Null;
			}

			m_orbitSystem.assignOrbitToEntity(liveEntity, loadedOrbit);

			if (m_transformSystem.hasTransform(liveEntity)) {
				m_transformSystem.getTransform(liveEntity).setPosition(loadedOrbit.absoluteWorldPosition);
			}
		}

		if (sceneRoot.contains("editor") && sceneRoot["editor"].contains("objects")) {
			for (const auto& jObj : sceneRoot["editor"]["objects"]) {
				uint32_t fileId = jObj["fileId"];
				if (fileToRuntimeMap.find(fileId) == fileToRuntimeMap.end()) continue;

				Entity liveEntity = fileToRuntimeMap[fileId];

				const std::string name = jObj.value("name", "Unnamed");
				const std::string tag = jObj.value("tag", "");
				const std::string modelName = jObj.value("modelName", "");

				m_nameSystem.addOrReplace(liveEntity, NameComponent{ name });
				m_tagSystem.addOrReplace(liveEntity, TagComponent{ tag });

				auto obj = m_sceneManager.bindEditorObject(liveEntity, name);
				obj->tag = tag;
				obj->modelName = modelName;

				if (jObj.contains("inheritFlags") && m_transformSystem.hasTransform(liveEntity)) {
					auto flags = jObj["inheritFlags"];
					auto& hierarchy = m_transformSystem.getHierarchy(liveEntity);
					hierarchy.inheritPosition = flags.value("position", true);
					hierarchy.inheritRotation = flags.value("rotation", true);
					hierarchy.inheritScale = flags.value("scale", true);

					obj->inheritPosition = hierarchy.inheritPosition;
					obj->inheritRotation = hierarchy.inheritRotation;
					obj->inheritScale = hierarchy.inheritScale;
				}

				if (!obj->modelName.empty()) {
					obj->model = m_assetManager.getModel(obj->modelName);
					m_modelSystem.addOrReplace(liveEntity, ModelComponent{ obj->modelName, obj->model });
				}

				if (m_transformSystem.hasTransform(liveEntity)) {
					obj->cachedEditorTransform = m_transformSystem.getTransform(liveEntity);
				}

				if (m_sceneManager.onObjectCreated) {
					m_sceneManager.onObjectCreated(obj);
				}

				if (obj->model && obj->constantBuffer && !m_renderSystem.has(liveEntity)) {
					RenderComponent renderable;
					renderable.model = obj->model.get();
					renderable.objectCB = obj->constantBuffer.get();
					renderable.castsShadow = true;
					renderable.visible = true;
					m_renderSystem.add(liveEntity, renderable);
				}
			}

			for (const auto& jObj : sceneRoot["editor"]["objects"]) {
				uint32_t fileId = jObj["fileId"];
				uint32_t parentFileId = jObj.value("parentFileId", static_cast<uint32_t>(-1));
				if (!fileToRuntimeMap.count(fileId)) continue;

				Entity childEnt = fileToRuntimeMap[fileId];
				Entity parentEnt = Entity::Null;
				if (parentFileId != static_cast<uint32_t>(-1) && fileToRuntimeMap.count(parentFileId)) {
					parentEnt = fileToRuntimeMap[parentFileId];
				}

				if (m_transformSystem.hasTransform(childEnt)) {
					m_transformSystem.setParent(childEnt, parentEnt);
				}

				auto childObj = m_sceneManager.findObjectByEntity(childEnt);
				auto parentObj = parentEnt.isNull() ? nullptr : m_sceneManager.findObjectByEntity(parentEnt);
				if (childObj) {
					childObj->setParent(parentObj);
				}
			}
		}

		return true;
	}

}