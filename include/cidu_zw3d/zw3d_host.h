#pragma once

#include "model_parameters.h"

#include <string>

namespace cidu_zw3d {

// 总结：集中封装 ZW3D 宿主 API，让业务代码不直接散落调用 SDK。
class Zw3DHost {
public:
    // 总结：注册一个无自定义处理逻辑的 ZW3D 命令占位。
    bool register_command(const std::wstring& name, const std::wstring& description);

    // 总结：注册一个带 C++ 回调函数的 ZW3D 命令。
    bool register_command_handler(const std::wstring& name, void* handler);

    // 总结：卸载指定 ZW3D 命令回调，避免插件退出后留下悬空函数指针。
    void unregister_command(const std::wstring& name);

    // 总结：在 ZW3D 内显示消息，非 SDK 环境下回退为系统消息框。
    void show_message(const std::wstring& message);

    // 总结：根据结构化参数执行真实或占位的参数化建模逻辑。
    ModelGenerationResult generate_model(const ModelParameters& parameters);
};

}  // namespace cidu_zw3d
