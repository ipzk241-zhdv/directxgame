#pragma once
#include <DX3D/Game/Entity.h>
#include <vector>
#include <algorithm>
#include <cassert>
#include <utility>

namespace dx3d {

    template<typename Component>
    class ComponentStorage {
    public:
        void add(Entity e, const Component& component) {
            const uint32_t entityIndex = e.getIndex();
            if (entityIndex >= m_sparse.size()) {
                m_sparse.resize(entityIndex + 1, INVALID_INDEX);
            }

            assert(m_sparse[entityIndex] == INVALID_INDEX && "Entity already has this component!");

            m_sparse[entityIndex] = m_denseData.size();
            m_denseData.push_back(component);
            m_denseEntities.push_back(e);
        }

        void addOrReplace(Entity e, const Component& component) {
            if (has(e)) {
                get(e) = component;
                return;
            }
            add(e, component);
        }

        void remove(Entity e) {
            assert(has(e) && "Entity does not have this component!");

            const uint32_t entityIndex = e.getIndex();
            const size_t deletedDenseIndex = m_sparse[entityIndex];
            const size_t lastDenseIndex = m_denseData.size() - 1;

            if (deletedDenseIndex != lastDenseIndex) {
                m_denseData[deletedDenseIndex] = std::move(m_denseData[lastDenseIndex]);
                Entity lastEntity = m_denseEntities[lastDenseIndex];
                m_denseEntities[deletedDenseIndex] = lastEntity;
                m_sparse[lastEntity.getIndex()] = deletedDenseIndex;
            }

            m_sparse[entityIndex] = INVALID_INDEX;
            m_denseData.pop_back();
            m_denseEntities.pop_back();
        }

        bool has(Entity e) const {
            const uint32_t entityIndex = e.getIndex();
            return entityIndex < m_sparse.size() && m_sparse[entityIndex] != INVALID_INDEX;
        }

        Component& get(Entity e) {
            assert(has(e) && "Entity does not have this component!");
            return m_denseData[m_sparse[e.getIndex()]];
        }

        const Component& get(Entity e) const {
            assert(has(e) && "Entity does not have this component!");
            return m_denseData[m_sparse[e.getIndex()]];
        }

        const std::vector<Entity>& getRawEntities() const { return m_denseEntities; }
        const std::vector<Component>& getRawData() const { return m_denseData; }

        void clear() {
            m_sparse.clear();
            m_denseData.clear();
            m_denseEntities.clear();
        }

    private:
        static constexpr size_t INVALID_INDEX = static_cast<size_t>(-1);
        std::vector<size_t> m_sparse;
        std::vector<Component> m_denseData;
        std::vector<Entity> m_denseEntities;
    };

}