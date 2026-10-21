#include "cidu_zw3d/cidu_client.h"
#include "cidu_zw3d/model_parameters.h"
#include "cidu_zw3d/plugin_config.h"
#include "cidu_zw3d/zw3d_host.h"

#include <filesystem>
#include <iostream>

namespace {

// 总结：把命令行参数转换成项目内部使用的宽字符串。
std::wstring widen_arg(const char* value) {
    std::string text(value ? value : "");
    return std::wstring(text.begin(), text.end());
}

}  // namespace

// 总结：提供本地命令行入口，用于调试 Cidu 调用和参数 JSON 解析。
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage:\n";
        std::cerr << "  cidu_zw3d_cli chat <prompt>\n";
        std::cerr << "  cidu_zw3d_cli generate <parameter-json>\n";
        return 2;
    }

    const std::string mode = argv[1];
    if (mode == "chat") {
        // chat 模式直接请求 Cidu，便于脱离 ZW3D 验证服务端配置。
        const auto settings = cidu_zw3d::load_cidu_settings("config/cidu_zw3d_config.json");
        cidu_zw3d::CiduClient client(settings);
        cidu_zw3d::ChatRequest request;
        // CLI 只传 query，不传 Agent 插件里的 response_schema，适合做最小连通性测试。
        request.query = widen_arg(argv[2]);
        const auto response = client.send_chat_message(request);
        std::wcout << L"ok=" << (response.ok ? L"true" : L"false") << L"\n";
        std::wcout << L"status=" << response.status_code << L"\n";
        std::wcout << L"answer=" << response.answer << L"\n";
        if (!response.error.empty()) {
            std::wcout << L"error=" << response.error << L"\n";
        }
        return response.ok ? 0 : 1;
    }

    if (mode == "generate") {
        // generate 模式只验证参数 JSON 到建模服务的本地链路。
        cidu_zw3d::ModelParameters parameters;
        if (!cidu_zw3d::from_json(widen_arg(argv[2]), parameters)) {
            std::cerr << "invalid parameter JSON\n";
            return 1;
        }
        cidu_zw3d::Zw3DHost host;
        // 非 ZW3D SDK 环境下 host.generate_model 会走占位实现，用于验证参数解析。
        const auto result = host.generate_model(parameters);
        std::wcout << result.message << L"\n";
        return result.ok ? 0 : 1;
    }

    std::cerr << "unknown mode\n";
    return 2;
}
