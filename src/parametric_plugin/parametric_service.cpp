#include "parametric_service.h"

namespace cidu_zw3d {

// 总结：保存 ZW3D 宿主封装引用。
ParametricService::ParametricService(Zw3DHost& host)
    : host_(host) {}

// 总结：初始化参数化服务，目前没有额外状态。
bool ParametricService::initialize() {
    return true;
}

// 总结：把解析好的模型参数交给 ZW3D 宿主层执行建模。
ModelGenerationResult ParametricService::generate(const ModelParameters& parameters) {
    return host_.generate_model(parameters);
}

}  // namespace cidu_zw3d
