#pragma once
#include <string>
#include <vector>
#include <memory>
#include <DX3D/Game/Entity.h>
#include <DX3D/Graphics/Resources/ModelGPU.h>

namespace dx3d {

    struct NameComponent {
        std::string name;
    };

    struct TagComponent {
        std::string tag;
    };

    struct ModelComponent {
        std::string modelName;
        std::shared_ptr<ModelGPU> model;
    };


}
