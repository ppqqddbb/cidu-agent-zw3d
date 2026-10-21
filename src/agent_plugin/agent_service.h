#pragma once

#include "cidu_zw3d/cidu_client.h"
#include "cidu_zw3d/model_parameters.h"
#include "cidu_zw3d/zw3d_host.h"

#include <filesystem>
#include <functional>
#include <vector>

namespace cidu_zw3d {

// 总结：描述 Cidu 选型出来的模型，以及该模型需要用户提供或确认的参数。
struct ModelSelectionInfo {
    // Cidu 选型得到的用户可读模型名称。
    std::wstring selected_model;
    // Cidu 选型得到的模板名，用于定位佳鱼参数化模型。
    std::wstring template_name;
    // 知识图谱返回的完整节点线路，用于在佳鱼自定义库树中定位模型。
    std::wstring full_node_path;
    // 对选中模型用途、结构或适用场景的说明。
    std::wstring model_description;
    // 参数需求说明，key 是参数名，value 描述单位、范围、是否必填等。
    std::map<std::wstring, std::wstring> parameter_requirements;
};

// 总结：承载 Agent 一次完整执行的展示信息，以及可选的参数化插件执行结果。
struct AgentRunResult {
    // 整体流程是否成功：选型版有可展示答案，或参数化插件执行成功。
    bool ok = false;
    // 参数化插件、选型工作流或 Agent 流程给出的最终消息。
    std::wstring message;
    // 参数化插件生成文件时返回的路径。
    std::wstring created_file;
    // Cidu 选型版工作流返回的 Markdown 答案。
    std::wstring answer_markdown;
    // 实际传给参数化插件的结构化建模参数。
    ModelParameters parameters;
    // 界面展示用的选型模型和参数说明。
    ModelSelectionInfo selection;
    // Cidu 返回的会话 ID，用于前端历史会话继续上下文。
    std::wstring conversation_id;
};

// 总结：接收 Cidu 智能体流式输出的 Markdown 或 JSON 文本片段。
using AgentStreamCallback = std::function<void(const std::wstring& delta)>;

// 总结：负责把用户自然语言请求发送给 Cidu，并兼容选型 Markdown 与结构化建模参数。
class AgentService {
public:
    // 总结：注入 Cidu 客户端和 ZW3D 宿主封装。
    AgentService(CiduClient client, Zw3DHost& host);

    // 总结：初始化 Agent 服务，目前预留后续资源检查。
    bool initialize();

    // 总结：调用 Cidu 获取选型结果；结构化参数版会继续交给参数化插件。
    AgentRunResult ask_and_generate(const std::wstring& user_prompt);

    // 总结：先上传本地附件，再调用 Cidu 获取选型结果。
    AgentRunResult ask_and_generate(
        const std::wstring& user_prompt,
        const std::vector<std::filesystem::path>& file_paths);

    // 总结：在指定 Cidu 会话里继续请求选型结果。
    AgentRunResult ask_and_generate(
        const std::wstring& user_prompt,
        const std::vector<std::filesystem::path>& file_paths,
        const std::wstring& conversation_id);

    // 总结：流式接收 Cidu 输出，结束后兼容 Markdown 选型结果或结构化建模参数。
    AgentRunResult ask_and_generate_streaming(
        const std::wstring& user_prompt,
        const std::vector<std::filesystem::path>& file_paths,
        const std::wstring& conversation_id,
        const AgentStreamCallback& on_delta);

private:
    CiduClient client_;
    Zw3DHost& host_;
};

}  // namespace cidu_zw3d
