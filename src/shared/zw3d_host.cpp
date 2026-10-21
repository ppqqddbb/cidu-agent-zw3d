#include "cidu_zw3d/zw3d_host.h"

#if defined(CIDU_ZW3D_WITH_REAL_SDK)
#include "zwapi_cmd.h"
#include "zwapi_message.h"
#endif

#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cidu_zw3d {

namespace {

// 总结：把命令名压成 ASCII，适配 ZW3D 旧式命令注册接口。
std::string narrow_ascii(const std::wstring& value) {
    std::string result;
    result.reserve(value.size());
    for (const wchar_t ch : value) {
        result.push_back(ch >= 0 && ch <= 127 ? static_cast<char>(ch) : '?');
    }
    return result;
}

// 总结：作为占位命令回调，保证只注册命令名时也有合法函数指针。
int zw3d_registered_command_stub(void) {
    return 0;
}

}  // namespace

// 总结：注册一个普通命令名，当前实现复用占位回调。
bool Zw3DHost::register_command(const std::wstring& name, const std::wstring& description) {
    (void)description;
    return register_command_handler(name, reinterpret_cast<void*>(zw3d_registered_command_stub));
}

// 总结：把 C++ 函数指针注册到 ZW3D 命令系统。
bool Zw3DHost::register_command_handler(const std::wstring& name, void* handler) {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    return cvxCmdFunc(narrow_ascii(name).c_str(), handler, 0.0) == 0;
#else
    (void)name;
    (void)handler;
    return true;
#endif
}

// 总结：从 ZW3D 命令系统卸载指定回调。
void Zw3DHost::unregister_command(const std::wstring& name) {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    cvxCmdFuncUnload(narrow_ascii(name).c_str());
#else
    (void)name;
#endif
}

// 总结：显示插件运行状态或错误信息。
void Zw3DHost::show_message(const std::wstring& message) {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    // ZW3D 消息接口只接受窄字符串，因此这里只显示 ASCII 安全内容。
    cvxMsgDisp(narrow_ascii(message).c_str());
#elif defined(_WIN32)
    MessageBoxW(nullptr, message.c_str(), L"Cidu ZW3D", MB_OK | MB_ICONINFORMATION);
#else
    (void)message;
#endif
}

// 总结：执行参数化建模；当前保留真实 SDK 建模逻辑的接入位置。
ModelGenerationResult Zw3DHost::generate_model(const ModelParameters& parameters) {
    if (parameters.template_name.empty()) {
        return {false, L"template_name is required.", L""};
    }
    // 真实建模逻辑应由参数化插件在这里按 template_name 分派到具体 ZW3D API。
    return {true, L"Stub model generation completed. Wire real ZW3D SDK calls in Zw3DHost.", L""};
}

}  // namespace cidu_zw3d
