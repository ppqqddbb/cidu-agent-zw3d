#include "parametric_service.h"

#include "cidu_zw3d/export.h"
#include "cidu_zw3d/json_protocol.h"
#include "cidu_zw3d/model_parameters.h"
#include "cidu_zw3d/zw3d_host.h"

#include <memory>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// 参数化插件自己的宿主封装实例，和 Agent 插件互相独立加载。
cidu_zw3d::Zw3DHost g_host;
// 参数化服务由 DLL 初始化入口创建，供命令和导出 API 复用。
std::unique_ptr<cidu_zw3d::ParametricService> g_service;
// 跨 DLL 返回宽字符串指针时使用静态缓存，避免返回局部变量地址。
std::wstring g_last_result;

// 总结：提前声明 JSON 建模导出函数，供命令回调复用。
extern "C" CIDU_ZW3D_API const wchar_t* CiduZW3DGenerateModelFromJson(const wchar_t* parameter_json);

// 总结：把 ZW3D 命令传入的 JSON 参数转换为内部宽字符串。
std::wstring widen_json_arg(const char* value) {
    if (!value || !*value) {
        return L"";
    }
#if defined(_WIN32)
    // 参数 JSON 通常来自 Agent 插件的 UTF-8 文本，失败时兼容系统本地编码。
    int size = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
    if (size <= 1) {
        size = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
        std::wstring text(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
        if (!text.empty()) {
            MultiByteToWideChar(CP_ACP, 0, value, -1, text.data(), size);
        }
        return text;
    }
    std::wstring text(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, -1, text.data(), size);
    return text;
#else
    std::string text(value);
    return std::wstring(text.begin(), text.end());
#endif
}

// 总结：处理手动命令形式的参数化建模调用。
int CiduZW3DGenerateModelCommand(char* parameter_json) {
    const wchar_t* result = CiduZW3DGenerateModelFromJson(widen_json_arg(parameter_json).c_str());
    g_host.show_message(result ? result : L"{\"ok\":false,\"message\":\"No result.\"}");
    return 0;
}

}  // namespace

// 总结：初始化参数化插件服务，并注册 JSON 建模命令。
extern "C" CIDU_ZW3D_API int CiduZW3DParametricInitialize() {
    g_service = std::make_unique<cidu_zw3d::ParametricService>(g_host);
    if (!g_service->initialize()) {
        return 1;
    }
    return g_host.register_command_handler(
        L"CiduZW3DGenerateModel",
        reinterpret_cast<void*>(CiduZW3DGenerateModelCommand)) ? 0 : 1;
}

// 总结：卸载参数化插件注册的命令并释放服务对象。
extern "C" CIDU_ZW3D_API int CiduZW3DParametricShutdown() {
    g_host.unregister_command(L"CiduZW3DGenerateModel");
    g_service.reset();
    return 0;
}

// 总结：导出给 Agent 插件调用的 JSON 参数化建模接口。
extern "C" CIDU_ZW3D_API const wchar_t* CiduZW3DGenerateModelFromJson(const wchar_t* parameter_json) {
    if (!g_service) {
        CiduZW3DParametricInitialize();
    }

    cidu_zw3d::ModelParameters parameters;
    if (!parameter_json || !cidu_zw3d::from_json(parameter_json, parameters)) {
        g_last_result = L"{\"ok\":false,\"message\":\"Invalid parameter JSON.\"}";
        return g_last_result.c_str();
    }

    // 参数化插件只关心 template_name/numeric/text，不解析 Agent 的展示字段。
    // 返回值保存在静态缓存中，保证调用方读取时指针仍有效。
    const auto result = g_service->generate(parameters);
    g_last_result = L"{\"ok\":";
    g_last_result += result.ok ? L"true" : L"false";
    g_last_result += L",\"message\":\"" + cidu_zw3d::json_escape(result.message) + L"\"";
    if (!result.created_file.empty()) {
        g_last_result += L",\"created_file\":\"" + cidu_zw3d::json_escape(result.created_file) + L"\"";
    }
    g_last_result += L"}";
    return g_last_result.c_str();
}

#if defined(_WIN32)
// 总结：Windows DLL 入口；当前不做额外处理，只保证 DLL 可加载。
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    return TRUE;
}
#endif
