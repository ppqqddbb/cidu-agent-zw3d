#include "agent_service.h"

#include "cidu_zw3d/json_protocol.h"
#include "cidu_zw3d/plugin_config.h"

#include <cwctype>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cidu_zw3d {

namespace {

using GenerateModelFromJsonFn = const wchar_t* (*)(const wchar_t*);

// 总结：修复流式/网关偶发把 Markdown 标题拆成单独 # 行的情况。
std::wstring normalize_markdown_headings(const std::wstring& value) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : value) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);

    const auto trim = [](const std::wstring& text) {
        std::size_t begin = 0;
        while (begin < text.size() && std::iswspace(text[begin])) {
            ++begin;
        }
        std::size_t end = text.size();
        while (end > begin && std::iswspace(text[end - 1])) {
            --end;
        }
        return text.substr(begin, end - begin);
    };
    const auto is_known_heading = [](const std::wstring& text) {
        return text == L"选型结果" ||
               text == L"选型依据" ||
               text == L"模型需要的参数" ||
               text == L"候选模型" ||
               text == L"识别到的检索线索";
    };

    std::vector<std::wstring> output;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::wstring line = trim(lines[index]);
        if (line == L"#" || line == L"##" || line == L"###") {
            std::size_t cursor = index + 1;
            int level = static_cast<int>(line.size());
            while (cursor < lines.size() && trim(lines[cursor]).empty()) {
                ++cursor;
            }
            if (cursor < lines.size() && trim(lines[cursor]) == L"#") {
                level = 2;
                ++cursor;
                while (cursor < lines.size() && trim(lines[cursor]).empty()) {
                    ++cursor;
                }
            }
            if (cursor < lines.size() && is_known_heading(trim(lines[cursor]))) {
                const int heading_level = level < 2 ? 2 : level;
                output.push_back(std::wstring(static_cast<std::size_t>(heading_level), L'#') + L" " + trim(lines[cursor]));
                index = cursor;
                continue;
            }
            continue;
        }
        if (is_known_heading(line)) {
            const int heading_level = line == L"选型结果" ? 2 : 3;
            output.push_back(std::wstring(static_cast<std::size_t>(heading_level), L'#') + L" " + line);
            continue;
        }
        output.push_back(lines[index]);
    }

    std::wstring normalized;
    for (std::size_t index = 0; index < output.size(); ++index) {
        if (index > 0) {
            normalized += L"\n";
        }
        normalized += output[index];
    }
    return normalized;
}

bool looks_like_sse_event_dump(const std::wstring& value) {
    return value.find(L"\"event\":\"workflow_started\"") != std::wstring::npos ||
           value.find(L"\"event\":\"node_started\"") != std::wstring::npos ||
           value.find(L"\"event\":\"workflow_finished\"") != std::wstring::npos ||
           value.find(L"data:") != std::wstring::npos;
}

// 总结：动态加载参数化插件 DLL，并调用它导出的 JSON 建模 API。
ModelGenerationResult call_parametric_plugin(const ModelParameters& parameters) {
#if !defined(_WIN32)
    (void)parameters;
    return {false, L"Parametric plugin invocation is only implemented on Windows.", L""};
#else
    // Agent 插件只负责转发参数，不直接承担真实建模逻辑。
    const std::filesystem::path plugin_path = module_directory() / L"CiduZW3D" / L"bin" / L"CiduParametricEngine.dll";
    HMODULE library = LoadLibraryW(plugin_path.c_str());
    if (!library) {
        return {false, L"Failed to load parametric plugin: " + plugin_path.wstring(), L""};
    }

    auto generate = reinterpret_cast<GenerateModelFromJsonFn>(
        GetProcAddress(library, "CiduZW3DGenerateModelFromJson"));
    if (!generate) {
        FreeLibrary(library);
        return {false, L"Parametric plugin API was not found: CiduZW3DGenerateModelFromJson", L""};
    }

    // 两个插件之间使用稳定的 JSON 字符串协议传递模型参数。
    const std::wstring parameter_json = to_json(parameters);
    const wchar_t* raw_response = generate(parameter_json.c_str());
    const std::wstring response = raw_response ? raw_response : L"";
    FreeLibrary(library);

    const bool ok = response.find(L"\"ok\":true") != std::wstring::npos;
    std::wstring message = json_string_value(response, L"message");
    if (message.empty()) {
        message = response.empty() ? L"Parametric plugin returned an empty response." : response;
    }
    return {ok, message, json_string_value(response, L"created_file")};
#endif
}

// 总结：把 Cidu 返回的选型字段解析成界面可展示的模型信息。
ModelSelectionInfo parse_selection_info(const std::wstring& json, const ModelParameters& parameters) {
    ModelSelectionInfo selection;
    selection.selected_model = json_string_value(json, L"selected_model");
    selection.template_name = json_string_value(json, L"template_name");
    selection.full_node_path = json_string_value(json, L"selected_full_node_path");
    if (selection.full_node_path.empty()) {
        selection.full_node_path = json_string_value(json, L"full_node_path");
    }
    selection.model_description = json_string_value(json, L"model_description");
    selection.parameter_requirements = json_string_object(json, L"parameter_requirements");

    // 兼容只返回旧协议的 Cidu 应用：没有 selected_model 时用 template_name 兜底展示。
    if (selection.selected_model.empty()) {
        selection.selected_model = parameters.template_name;
    }
    if (selection.template_name.empty()) {
        selection.template_name = parameters.template_name;
    }
    return selection;
}

// 总结：根据文件扩展名推断 Cidu 聊天接口需要的附件类型。
std::wstring chat_file_type_for_path(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    for (auto& ch : extension) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }

    if (extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".gif" ||
        extension == L".webp" || extension == L".bmp") {
        return L"image";
    }
    if (extension == L".mp3" || extension == L".wav" || extension == L".m4a" || extension == L".aac" ||
        extension == L".ogg") {
        return L"audio";
    }
    if (extension == L".mp4" || extension == L".mov" || extension == L".avi" || extension == L".mkv" ||
        extension == L".webm") {
        return L"video";
    }
    return L"document";
}

// 总结：构造发送给 Cidu 智能体的标准请求，统一阻塞和流式模式的提示协议。
ChatRequest build_agent_request(const std::wstring& user_prompt) {
    ChatRequest request;
    request.query = user_prompt;
    return request;
}

struct SplitAskPrompt {
    std::wstring calculate_query;
};

SplitAskPrompt split_calculate_prompt(const std::wstring& user_prompt) {
    SplitAskPrompt result;
    result.calculate_query = user_prompt;
    // 新版工作流支持在同一请求中先写入输入/计算结果修改，再立即执行公式计算。
    // 必须保留“开始计算 + 全部变更行”的原子请求；旧的两次请求方案可能使
    // 参数修改请求在网关超时后丢失，随后“开始计算”只能继续使用旧参数。
    return result;
}

std::wstring merge_conversation_id(
    const std::wstring& current,
    const std::wstring& candidate) {
    return candidate.empty() ? current : candidate;
}

// 总结：上传本地附件并把返回的文件 ID 挂到 Cidu 聊天请求上。
AgentRunResult attach_uploaded_files(
    CiduClient& client,
    ChatRequest& request,
    const std::vector<std::filesystem::path>& file_paths) {
    for (const auto& file_path : file_paths) {
        const FileUploadResponse upload = client.upload_file(file_path);
        if (!upload.ok) {
            return {
                false,
                L"Cidu file upload failed: " + upload.error,
                L"",
                L"",
                {},
                {},
                L""};
        }

        ChatFile chat_file;
        chat_file.type = chat_file_type_for_path(file_path);
        chat_file.upload_file_id = upload.file.id;
        request.files.push_back(chat_file);
    }
    return {true, L"", L"", L"", {}, {}, L""};
}

// 总结：解析 Cidu 完整输出；选型版 Markdown 直接返回，结构化参数版继续建模。
AgentRunResult generate_from_cidu_response(const ChatResponse& response) {
    if (!response.ok) {
        return {false, L"Cidu request failed: " + response.error, L"", L"", {}, {}, response.conversation_id};
    }

    ModelParameters parameters;
    // 优先解析 answer；如果 Cidu 应用直接把 JSON 放在原始响应里，也保留兜底。
    const std::wstring candidate_json = response.answer;
    if (!from_json(candidate_json, parameters)) {
        const std::wstring answer = normalize_markdown_headings(response.answer);
        const bool valid_answer = !answer.empty() && !looks_like_sse_event_dump(answer);
        return {
            valid_answer,
            valid_answer ? answer : L"Cidu did not return a selection answer.",
            L"",
            valid_answer ? answer : L"",
            {},
            {},
            response.conversation_id};
    }

    const ModelSelectionInfo selection = parse_selection_info(candidate_json, parameters);
    const ModelGenerationResult generation = call_parametric_plugin(parameters);
    // AgentRunResult 把“界面展示信息”和“建模执行结果”合并返回给插件入口层。
    return {
        generation.ok,
        generation.message,
        generation.created_file,
        L"",
        parameters,
        selection,
        response.conversation_id};
}

}  // namespace

// 总结：保存 Cidu 客户端和宿主引用。
AgentService::AgentService(CiduClient client, Zw3DHost& host)
    : client_(std::move(client)), host_(host) {}

// 总结：初始化 Agent 服务，目前没有额外资源需要加载。
bool AgentService::initialize() {
    return true;
}

// 总结：请求 Cidu 生成选型结果。
AgentRunResult AgentService::ask_and_generate(const std::wstring& user_prompt) {
    return ask_and_generate(user_prompt, {});
}

// 总结：上传本地附件后，请求 Cidu 生成选型结果。
AgentRunResult AgentService::ask_and_generate(
    const std::wstring& user_prompt,
    const std::vector<std::filesystem::path>& file_paths) {
    return ask_and_generate(user_prompt, file_paths, L"");
}

// 总结：上传本地附件后，在指定 Cidu 会话里请求选型结果。
AgentRunResult AgentService::ask_and_generate(
    const std::wstring& user_prompt,
    const std::vector<std::filesystem::path>& file_paths,
    const std::wstring& conversation_id) {
    const SplitAskPrompt split = split_calculate_prompt(user_prompt);
    std::wstring active_conversation_id = conversation_id;
    // 新版工作流以单个原子请求接收“开始计算 + 修改项”，不再执行前置同步请求。

    ChatRequest request = build_agent_request(split.calculate_query);
    request.conversation_id = active_conversation_id;
    const AgentRunResult upload_result = attach_uploaded_files(client_, request, file_paths);
    if (!upload_result.ok) {
        return upload_result;
    }

    // Cidu 是服务端智能体，本机只负责展示选型结果或消费结构化参数。
    const ChatResponse response = client_.send_chat_message(request);
    AgentRunResult result = generate_from_cidu_response(response);
    result.conversation_id = merge_conversation_id(active_conversation_id, result.conversation_id);
    return result;
}

// 总结：流式调用 Cidu 智能体，边输出边累积完整答案，结束后兼容 Markdown 或结构化参数。
AgentRunResult AgentService::ask_and_generate_streaming(
    const std::wstring& user_prompt,
    const std::vector<std::filesystem::path>& file_paths,
    const std::wstring& conversation_id,
    const AgentStreamCallback& on_delta) {
    const SplitAskPrompt split = split_calculate_prompt(user_prompt);
    std::wstring active_conversation_id = conversation_id;
    // 新版工作流以单个原子请求接收“开始计算 + 修改项”，不再执行前置同步请求。

    ChatRequest request = build_agent_request(split.calculate_query);
    request.conversation_id = active_conversation_id;
    const AgentRunResult upload_result = attach_uploaded_files(client_, request, file_paths);
    if (!upload_result.ok) {
        return upload_result;
    }

    // Cidu 流式输出负责前端即时展示，最终用完整 answer 做 Markdown/参数兼容处理。
    ChatResponse response = client_.stream_chat_message(request, on_delta);
    if ((!response.ok && response.status_code == 0) ||
        response.answer.empty() ||
        looks_like_sse_event_dump(response.answer)) {
        // 服务端或网关没有给出可展示的 SSE answer 时，自动退回阻塞模式。
        // Dify advanced-chat 的阻塞响应会直接返回最终 answer，避免把 workflow/node 事件当正文。
        response = client_.send_chat_message(request);
    }
    AgentRunResult result = generate_from_cidu_response(response);
    result.conversation_id = merge_conversation_id(active_conversation_id, result.conversation_id);
    return result;
}

}  // namespace cidu_zw3d
