#pragma once
#include <d3d11.h>

namespace hooks {
    bool init();
    void shutdown();
    ID3D11Device* d3d_device();
    ID3D11DeviceContext* d3d_context();
}