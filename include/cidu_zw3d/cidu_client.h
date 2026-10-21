#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cidu_zw3d {

// 总结：保存调用 Cidu 服务端接口所需的基础配置。
struct CiduSettings {
    // Cidu 服务端 API 根地址，通常是 http://host/v1。
    std::wstring base_url;
    // Cidu 应用 API Key，留空时按无鉴权方式请求。
    std::wstring api_key;
    // Cidu 会话用户标识，用于服务端区分不同调用方。
    std::wstring user;
    // 网络连接、发送、接收共用的超时时间，单位为秒。
    int timeout_seconds = 900;
    // 佳鱼 MySQL 地址，留空时不执行知识图谱更新。
    std::wstring jiavu_db_host;
    // 佳鱼 MySQL 端口；留空时同步工具按 3306 处理。
    std::wstring jiavu_db_port;
    // 佳鱼数据库名；留空时同步工具会从非系统库中自动探测。
    std::wstring jiavu_db_name;
    // 佳鱼数据库只读用户名。
    std::wstring jiavu_db_user;
    // 佳鱼数据库密码。
    std::wstring jiavu_db_password;
    // Dify 知识同步 API 根地址，通常是 http://host/v1；留空时可复用智能体地址。
    std::wstring dify_sync_base_url;
    // Dify 知识库 Dataset API Key，独立于智能体 App API Key。
    std::wstring dify_dataset_api_key;
    // Dify 知识库 ID，用于同步佳鱼转换后的知识库文档。
    std::wstring dify_dataset_id;
    // 自定义 Dify 知识图谱 ID，用于同步 Neo4j 图谱。
    std::wstring dify_knowledge_graph_id;
    // Dify 知识库索引方式，常用 economy 或 high_quality。
    std::wstring dify_dataset_indexing_technique = L"economy";
    // Dify 知识库中按条目创建文档时使用的名称前缀。
    std::wstring dify_knowledge_base_document_name = L"静态配置";
    // 生成模型时写入给佳鱼读取的 Excel 模板目录；留空时不允许生成。
    std::wstring jiavu_model_excel_dir;
    // jyasm 解密服务的内网地址，例 http://192.168.x.y:8765。
    // 留空表示不调 jyasm-service。
    std::wstring jyasm_service_base_url;
    // jyasm 解密服务的 Bearer Token；/products/.../manifest 等接口需要。
    std::wstring jyasm_service_token;
    // 同步/Agent 引用图片时拼出来的对外 URL 基址，例 http://192.168.x.y:8765。
    // 跟 base_url 可以不同（比如内网走 base_url、最终给用户的 markdown 引用走 public）。
    std::wstring jyasm_service_public_base_url;
    // 同步任务是否在拉 manifest 之前先 POST /refresh；写到 config 让 C# 同步工具读。
    std::wstring jyasm_service_trigger_refresh = L"true";
    // jyasm-service HTTP 超时，毫秒。
    std::wstring jyasm_service_timeout_ms = L"900000";
};

// 总结：描述已经上传到 Cidu 服务端、可被聊天接口引用的文件。
struct UploadedFile {
    // Cidu `/files/upload` 返回的文件 ID。
    std::wstring id;
    // Cidu 返回或本地上传时使用的文件名。
    std::wstring name;
};

// 总结：描述发送 `/chat-messages` 时附带的文件引用。
struct ChatFile {
    // Cidu 文件类型，常见值为 document、image、audio、video。
    std::wstring type = L"document";
    // 当前插件先上传到服务端，再按 local_file 方式引用。
    std::wstring transfer_method = L"local_file";
    // `/files/upload` 返回的文件 ID。
    std::wstring upload_file_id;
};

// 总结：承载 Cidu 文件上传接口返回的状态、文件 ID 和错误信息。
struct FileUploadResponse {
    // HTTP 状态码在 2xx 范围内并且解析到文件 ID 时为 true。
    bool ok = false;
    // Cidu HTTP 响应状态码，用于定位鉴权、路由或服务端错误。
    long status_code = 0;
    // 上传成功后可被聊天接口引用的文件信息。
    UploadedFile file;
    // 原始响应体，便于调试 Cidu 服务端返回。
    std::wstring raw_body;
    // 请求失败时保存错误响应体或本地 WinHTTP 错误描述。
    std::wstring error;
};

// 总结：承载 Cidu/Dify JSON API 返回的原始响应，供前端继续解析历史会话等结构。
struct JsonApiResponse {
    // HTTP 状态码在 2xx 范围内时为 true。
    bool ok = false;
    // Cidu HTTP 响应状态码，用于定位鉴权、路由或服务端错误。
    long status_code = 0;
    // 原始 JSON 响应体。
    std::wstring raw_body;
    // 请求失败时保存错误响应体或本地 WinHTTP 错误描述。
    std::wstring error;
};

// 总结：描述一次发送给 Cidu 聊天接口的请求内容。
struct ChatRequest {
    // 用户在插件窗口或命令行输入的自然语言建模需求。
    std::wstring query;
    // 传给 Cidu workflow/chat app 的结构化输入变量。
    std::map<std::wstring, std::wstring> inputs;
    // 可选的 Cidu 会话 ID，当前插件默认每次独立请求。
    std::wstring conversation_id;
    // 已上传到 Cidu 服务端的文件引用，会随聊天请求一起传给智能体。
    std::vector<ChatFile> files;
};

// 总结：承载 Cidu 聊天接口返回的状态、正文和错误信息。
struct ChatResponse {
    // HTTP 状态码在 2xx 范围内时为 true。
    bool ok = false;
    // Cidu HTTP 响应状态码，用于定位鉴权、路由或服务端错误。
    long status_code = 0;
    // Cidu 标准响应中的 answer 字段，通常承载 Agent 输出。
    std::wstring answer;
    // 原始响应体，answer 解析失败时也可作为兜底 JSON 来源。
    std::wstring raw_body;
    // 请求失败时保存错误响应体或本地 WinHTTP 错误描述。
    std::wstring error;
    // Cidu 返回的会话 ID，后续同一历史会话继续提问时会带回服务端。
    std::wstring conversation_id;
};

// 总结：接收 Cidu 流式响应中的 answer 增量文本。
using StreamDeltaCallback = std::function<void(const std::wstring& delta)>;

// 总结：封装 Cidu `/chat-messages` 阻塞式聊天接口调用。
class CiduClient {
public:
    // 总结：保存外部传入的 Cidu 配置，供后续请求复用。
    explicit CiduClient(CiduSettings settings);

    // 总结：把用户请求发送到 Cidu，并解析基础响应字段。
    ChatResponse send_chat_message(const ChatRequest& request) const;

    // 总结：以 SSE 流式模式请求 Cidu，并在结束后返回累积完整 answer。
    ChatResponse stream_chat_message(
        const ChatRequest& request,
        const StreamDeltaCallback& on_delta) const;

    // 总结：把本地文件上传到 Cidu，并返回可放入聊天请求的文件 ID。
    FileUploadResponse upload_file(const std::filesystem::path& path) const;

    // 总结：按当前 user 从 Dify 拉取历史会话列表。
    JsonApiResponse list_conversations(std::size_t limit = 50) const;

    // 总结：按当前 user 和 conversation_id 从 Dify 拉取会话消息。
    JsonApiResponse list_messages(const std::wstring& conversation_id, std::size_t limit = 100) const;

private:
    CiduSettings settings_;
};

}  // namespace cidu_zw3d
