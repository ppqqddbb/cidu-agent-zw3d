#pragma once

#include "cidu_zw3d/model_parameters.h"
#include "cidu_zw3d/zw3d_host.h"

namespace cidu_zw3d {

// 总结：参数化插件的业务服务，负责把结构化参数交给 ZW3D 建模层。
class ParametricService {
public:
    // 总结：注入 ZW3D 宿主封装，隔离具体 SDK 调用。
    explicit ParametricService(Zw3DHost& host);

    // 总结：初始化参数化服务，目前预留后续模板加载。
    bool initialize();

    // 总结：根据参数对象触发模型生成。
    ModelGenerationResult generate(const ModelParameters& parameters);

private:
    Zw3DHost& host_;
};

}  // namespace cidu_zw3d
