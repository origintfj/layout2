#include "PrimitiveRenderBackend.h"

#include "OpenGLPrimitiveRenderBackend.h"
#include "RasterPrimitiveRenderBackend.h"

std::unique_ptr<PrimitiveRenderBackend> createPrimitiveRenderBackend(const RenderTypes::BackendType backendType) {
    if (backendType == RenderTypes::BackendType::OpenGL) {
        return std::make_unique<OpenGLPrimitiveRenderBackend>();
    }

    return std::make_unique<RasterPrimitiveRenderBackend>();
}
