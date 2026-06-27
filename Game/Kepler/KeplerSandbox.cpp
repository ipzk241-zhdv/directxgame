#include <Game/Kepler/KeplerSandbox.h>
#include <DX3D/InputSystem/InputSystem.h>
#include <DX3D/Graphics/Rendering/GraphicsEngine.h>
#include <DX3D/Game/Display.h>
#include <DX3D/Core/Logger.h>
#include <DX3D/Math/Vec3d.h>
#include <Game/Editor/HierarchyPanel.h>
#include <Game/Editor/InspectorPanel.h>
#include <Game/Editor/TimelinePanel.h>
#include <Game/Editor/AssetBrowserPanel.h>
#include <Game/Components/OrbitComponent.h>
#include <Game/Components/OrbitVisualizerComponent.h>
#include <DX3D/Game/SceneSerializer.h>
#include <imgui.h>
#include <algorithm>

namespace dx3d
{
	KeplerSandbox::KeplerSandbox(const GameDesc& desc) : Game(desc)
	{
		m_scene.setRegistry(&m_world.registry);
		m_hierarchyBridge = std::make_unique<HierarchyBridge>(m_scene, m_world.transforms);

		m_scene.resolveTransform = [this](Entity e) -> const Transform* {
			if (!m_world.transforms.hasTransform(e)) {
				return nullptr;
			}
			return &m_world.transforms.getTransform(e);
			};


		m_scene.assignTransform = [this](Entity e, const Transform& t) {
			if (m_world.transforms.hasTransform(e)) {
				m_world.transforms.setTransform(e, t);
			}
			else {
				m_world.transforms.assignTransform(e, t);
			}
			};

		m_scene.onObjectCreated = [this](std::shared_ptr<GameObject> obj) {
			m_world.names.addOrReplace(obj->entity, NameComponent{ obj->name });
			m_world.tags.addOrReplace(obj->entity, TagComponent{ obj->tag });
			if (!obj->modelName.empty() || obj->model) {
				m_world.models.addOrReplace(obj->entity, ModelComponent{ obj->modelName, obj->model });
			}

			obj->constantBuffer = m_graphicsEngine->getGraphicsDevice().createConstantBuffer({ nullptr, sizeof(DirectX::XMFLOAT4X4) * 3 });

			if (obj->model) {
				RenderComponent rc;
				rc.model = obj->model.get();
				rc.objectCB = obj->constantBuffer.get();
				rc.castsShadow = true;
				rc.visible = true;

				if (!m_world.renderables.has(obj->entity)) {
					m_world.renderables.add(obj->entity, rc);
				}
			}
			};

		// load default scene
		this->onKeyDown(VK_F7);

		m_timeController.SetTimeWarpByIndex(2);
		auto lights = m_graphicsEngine->getLightManager();
		lights->clear();
		lights->addDirectional(DirectX::XMFLOAT3(0.f, -1.f, 0.2f), DirectX::XMFLOAT3(1.f, 1.f, 1.f), 1.2f, true);

		m_editor = std::make_unique<KeplerEditor>(m_scene, m_world.transforms, m_world.renderables, m_world.orbitSystem, m_timeController, *m_camera, *m_assets, m_display.get(), *m_graphicsEngine);
		m_editor->init();
	}

	KeplerSandbox::~KeplerSandbox() = default;

	void KeplerSandbox::rebuildECSStoreFromScene()
	{
		m_world.transforms.clear();
		m_world.renderables.clear();
		m_world.names.clear();
		m_world.tags.clear();
		m_world.models.clear();

		for (const auto& obj : m_scene.getAllObjects()) {
			m_world.transforms.assignTransform(obj->entity, obj->cachedEditorTransform);
			m_world.names.addOrReplace(obj->entity, NameComponent{ obj->name });
			m_world.tags.addOrReplace(obj->entity, TagComponent{ obj->tag });
			if (!obj->modelName.empty() || obj->model) {
				m_world.models.addOrReplace(obj->entity, ModelComponent{ obj->modelName, obj->model });
			}

			if (obj->model) {
				RenderComponent rc;
				rc.model = obj->model.get();
				rc.objectCB = obj->constantBuffer.get();
				rc.castsShadow = true;
				rc.visible = true;

				m_world.renderables.add(obj->entity, rc);
			}
		}

		m_hierarchyBridge->syncEditorToRuntime();
		assert(m_hierarchyBridge->validateHierarchyConsistency() && "Hierarchy bridge failed to sync!");
	}

	// Editor bridge only: mirrors ECS transforms into GameObject cache for inspector/hierarchy UI.
	void KeplerSandbox::mirrorTransformsToSceneObjects()
	{
		m_scene.syncAllObjectTransformsFromECS();
	}

	void KeplerSandbox::onWindowResized(int width, int height)
	{
		Game::onWindowResized(width, height);
		m_editor->onWindowResized(width, height);
	}

	void KeplerSandbox::onUpdate(double dt, double fdt)
	{
		auto simStart = std::chrono::high_resolution_clock::now();
		m_timeController.Update(dt);
		double scaledDt = m_timeController.GetScaledDeltaTime(dt);

		m_world.orbitSystem.UpdateAll(scaledDt);

		m_world.orbitSystem.forEach([this](Entity e, Simulator::OrbitData& orbit) {
			if (m_world.transforms.hasTransform(e)) {
				m_world.transforms.getTransform(e).setPosition(orbit.absoluteWorldPosition);
				m_world.transforms.markTransformDirty(e);
			}
			auto it = m_orbitVisualizers.find(e.id);
			if (it != m_orbitVisualizers.end()) {
				it->second.update(m_graphicsEngine->getGraphicsDevice(), orbit);
			}
			});

		auto simEnd = std::chrono::high_resolution_clock::now();
		m_metrics.simulationTimeMs = std::chrono::duration<double, std::milli>(simEnd - simStart).count();

		auto transformStart = std::chrono::high_resolution_clock::now();
		m_world.transforms.updateWorldTransforms();
		auto transformEnd = std::chrono::high_resolution_clock::now();
		m_metrics.transformTimeMs = std::chrono::duration<double, std::milli>(transformEnd - transformStart).count();

		mirrorTransformsToSceneObjects();
		syncCameraOrbitTarget();

		m_metrics.activeEntities = m_world.registry.aliveCount();
		m_metrics.orbitCount = m_world.orbitSystem.getRawData().size();
	}

	void dx3d::KeplerSandbox::onGUI()
	{
		if (m_showProfiler)
		{
			ImGui::SetNextWindowPos(ImVec2(300.0f, 100.0f), ImGuiCond_FirstUseEver);

			if (ImGui::Begin("Runtime Profiler", &m_showProfiler))
			{
				ImGui::Text("Active Entities: %zu", m_metrics.activeEntities);
				ImGui::Text("Active Orbits: %zu", m_metrics.orbitCount);
				ImGui::Text("Draw Calls (Orbits): %zu", m_metrics.drawCalls);
				ImGui::Separator();
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Simulation Time: %.3f ms", m_metrics.simulationTimeMs);
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Transform Sync: %.3f ms", m_metrics.transformTimeMs);
				ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Render Dispatch: %.3f ms", m_metrics.renderBuildTimeMs);
			}
			ImGui::End();
		}

		m_editor->onGUI();
	}

	void KeplerSandbox::onDrawDebug(DeviceContext& ctx, const DirectX::XMFLOAT4X4& view, const DirectX::XMFLOAT4X4& proj)
	{
		auto renderStart = std::chrono::high_resolution_clock::now();
		m_metrics.drawCalls = 0;

		m_world.orbitSystem.forEach([this, &ctx, &view, &proj](Entity e, Simulator::OrbitData& orbit) {
			if (orbit.ParentEntity != dx3d::Entity::Null)
			{
				auto it = m_orbitVisualizers.find(e.id);
				if (it != m_orbitVisualizers.end())
				{
					it->second.draw(ctx, view, proj, orbit, m_camera->getPosition());
					m_metrics.drawCalls++;
				}
			}
			});
		auto renderEnd = std::chrono::high_resolution_clock::now();
		m_metrics.renderBuildTimeMs = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count();

		m_editor->onDrawDebug(ctx, view, proj);
	}

	void KeplerSandbox::syncCameraOrbitTarget() {
		Entity selectedEntity = m_editor->getSelectedEntity();
		if (!selectedEntity.isNull() && m_camera->isOrbiting()) {
			if (m_world.orbitSystem.hasOrbit(selectedEntity) && m_world.transforms.hasTransform(selectedEntity)) {
				m_camera->setOrbitTarget(m_world.transforms.getWorld(selectedEntity).position);
			}
			else if (m_world.transforms.hasTransform(selectedEntity)) {
				m_camera->setOrbitTarget(m_world.transforms.getTransform(selectedEntity).getPosition());
			}
		}
	}

	void KeplerSandbox::onMouseDown(int button)
	{
		if (ImGui::GetIO().WantCaptureMouse) return;

		if (button == 0)
		{
			auto mouseState = InputSystem::get()->getMouseState();
			int width = m_display->getClientWidth();
			int height = m_display->getClientHeight();

			DirectX::XMVECTOR dir;
			dx3d::Vec3d origin;
			m_camera->screenPointToRay(mouseState.coords.x, mouseState.coords.y, width, height, origin, dir);

			std::shared_ptr<GameObject> pick = m_scene.pickObject(origin, dir, m_camera->getPosition());

			m_editor->setSelectedEntity(pick ? pick->entity : Entity::Null);
		}
	}

	void KeplerSandbox::onKeyDown(int key)
	{
		if (key == 'F')
		{
			Entity selectedEntity = m_editor->getSelectedEntity();
			if (!selectedEntity.isNull())
			{
				bool isOrbiting = !m_camera->isOrbiting();
				m_camera->setOrbitMode(isOrbiting);

				if (isOrbiting) {
					syncCameraOrbitTarget();
				}
			}
			else
			{
				m_camera->setOrbitMode(false);
			}
		}
		if (key == VK_OEM_PERIOD)
		{
			m_timeController.IncreaseWarp();
		}
		if (key == VK_OEM_COMMA)
		{
			m_timeController.DecreaseWarp();
		}
		if (key == 'P')
		{
			m_timeController.SetPaused(!m_timeController.IsPaused());
		}

		if (key == VK_F5)
		{
			SceneSerializer serializer(m_world.registry, m_world.transforms, m_world.renderables, m_world.names, m_world.tags, m_world.models, m_world.orbitSystem, m_scene, *m_assets);
			if (serializer.Serialize("quicksave.json")) {
				DX3D_LOG_INFO("Quick saved to quicksave.json (V2 Format)");
			}
		}

		if (key == VK_F6 || key == VK_F7)
		{
			SceneSerializer serializer(m_world.registry, m_world.transforms, m_world.renderables, m_world.names, m_world.tags, m_world.models, m_world.orbitSystem, m_scene, *m_assets);
			auto saveFilePath = key == VK_F6 ? "quicksave.json" : "default_scene.json";
			if (serializer.Deserialize(saveFilePath)) {
				DX3D_LOG_INFO("Scene loaded from {}", saveFilePath);

				m_hierarchyBridge->syncEditorToRuntime();
				m_orbitVisualizers.clear();
				m_world.orbitSystem.forEach([this](Entity e, Simulator::OrbitData& orbit) {
					m_orbitVisualizers[e.id].init(m_graphicsEngine->getGraphicsDevice());
					});
			}
			else {
				DX3D_LOG_ERROR("Failed to load {}. File may be missing or in the deprecated format.", saveFilePath);
			}
		}

		if (key == VK_F1)
		{
			m_showProfiler = !m_showProfiler;
		}
	}

	void KeplerSandbox::stressTest()
	{
		constexpr int NUM_PLANETS = 0;

		auto sunObj = m_scene.findObject("Sun");
		auto planetModel = m_assets->getModel("sphere.obj");

		if (sunObj && planetModel && NUM_PLANETS > 0)
		{
			DX3D_LOG_INFO("Spawning {} stress test planets...", NUM_PLANETS);
			double G = 1.0;
			double M = 1000000.0;

			for (int i = 0; i < NUM_PLANETS; ++i)
			{
				auto obj = m_scene.createObject("StressPlanet_" + std::to_string(i));
				obj->model = planetModel;
				obj->cachedEditorTransform.setScale(DirectX::XMFLOAT3(2.0f, 2.0f, 2.0f));

				m_scene.applyTransformToEntity(obj->entity, obj->cachedEditorTransform);

				if (m_scene.onObjectCreated) {
					m_scene.onObjectCreated(obj);
				}

				double R = 150.0 + (i * 8.0);
				double V = std::sqrt((G * M) / R);

				Simulator::OrbitData orbit;
				orbit.ParentEntity = sunObj->entity;
				orbit.AttractorMass = M;
				orbit.BodyMass = 1.0;
				orbit.GravConst = G;

				orbit.positionRelativeToAttractor = { R, 0.0, 0.0 };
				orbit.velocityRelativeToAttractor = { 0.0, 0.0, V };

				orbit.elementsDirty = true;
				orbit.visualDirty = true;

				m_world.orbitSystem.assignOrbitToEntity(obj->entity, orbit);

				m_orbitVisualizers[obj->entity.id].init(m_graphicsEngine->getGraphicsDevice());
			}

			m_hierarchyBridge->syncEditorToRuntime();
		}
	}
}