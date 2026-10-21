#include "cidu_zw3d/cidu_client.h"

#include "cidu_zw3d/json_protocol.h"

#include <cwctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

namespace cidu_zw3d {

namespace {

// 总结：组装 Cidu 聊天接口需要的 JSON 请求体，可切换 blocking 或 streaming。
std::wstring build_chat_body(
    const CiduSettings& settings,
    const ChatRequest& request,
    const std::wstring& response_mode) {
    std::wostringstream out;
    out << L"{\"inputs\":{";
    bool first = true;
    for (const auto& [key, value] : request.inputs) {
        if (!first) {
            out << L",";
        }
        out << L"\"" << json_escape(key) << L"\":\"" << json_escape(value) << L"\"";
        first = false;
    }
    out << L"},\"query\":\"" << json_escape(request.query) << L"\",";
    out << L"\"response_mode\":\"" << json_escape(response_mode) << L"\",";
    out << L"\"user\":\"" << json_escape(settings.user) << L"\"";
    // 附件走 Dify 顶层 files 字段（features.file_upload 通道），
    // 对应下游引用 {{#sys.files#}}。
    if (!request.files.empty()) {
        out << L",\"files\":[";
        bool first_file = true;
        for (const auto& file : request.files) {
            if (!first_file) {
                out << L",";
            }
            out << L"{\"type\":\"" << json_escape(file.type) << L"\",";
            out << L"\"transfer_method\":\"" << json_escape(file.transfer_method) << L"\",";
            out << L"\"upload_file_id\":\"" << json_escape(file.upload_file_id) << L"\"}";
            first_file = false;
        }
        out << L"]";
    }
    if (!request.conversation_id.empty()) {
        out << L",\"conversation_id\":\"" << json_escape(request.conversation_id) << L"\"";
    }
    out << L"}";
    return out.str();
}

#if defined(_WIN32)
// 总结：把内部宽字符串转换成发送 HTTP 请求所需的 UTF-8 字节串。
std::string narrow_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string output(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), size, nullptr, nullptr);
    return output;
}

// 总结：把 Cidu 返回的 UTF-8 响应体转换成项目内部使用的宽字符串。
std::wstring widen_utf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size);
    return output;
}

// 总结：解析 Cidu base_url，拆出协议、主机、端口和路径。
bool parse_base_url(const std::wstring& base_url, URL_COMPONENTS& components) {
    ZeroMemory(&components, sizeof(components));
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    return WinHttpCrackUrl(base_url.c_str(), 0, 0, &components) == TRUE;
}

// 总结：从 Cidu base_url 追加接口路径，兼容 base_url 已经包含 `/v1` 的配置。
std::wstring api_path(const URL_COMPONENTS& components, const std::wstring& endpoint) {
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (!path.empty() && path.back() == L'/') {
        path.pop_back();
    }
    return path + endpoint;
}

// 总结：从 SSE 行里提取 data 字段内容，并去掉协议允许的单个前导空格。
bool sse_data_value(const std::string& line, std::string& value) {
    constexpr char prefix[] = "data:";
    if (line.rfind(prefix, 0) != 0) {
        return false;
    }

    value = line.substr(sizeof(prefix) - 1);
    if (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
    }
    return true;
}

// 总结：只允许 Dify/Cidu 的聊天消息事件进入正文流，避免节点完成事件里的嵌套 answer 重复显示。
bool is_chat_message_event(const std::wstring& event) {
    return event.empty() || event == L"message" || event == L"agent_message";
}

// 总结：确认字段是 SSE 顶层字段，避免 json_string_value 命中 data.outputs.answer 等嵌套字段。
bool has_top_level_key(const std::wstring& json, const std::wstring& key) {
    const std::wstring quoted_key = L"\"" + key + L"\"";
    int depth = 0;
    bool in_string = false;
    for (std::size_t pos = 0; pos < json.size(); ++pos) {
        const wchar_t ch = json[pos];
        if (!in_string && depth == 1 && json.compare(pos, quoted_key.size(), quoted_key) == 0) {
            std::size_t cursor = pos + quoted_key.size();
            while (cursor < json.size() && std::iswspace(json[cursor])) {
                ++cursor;
            }
            return cursor < json.size() && json[cursor] == L':';
        }
        if (ch == L'\\' && in_string) {
            ++pos;
            continue;
        }
        if (ch == L'"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == L'{') {
            ++depth;
            continue;
        }
        if (ch == L'}') {
            --depth;
            continue;
        }
    }
    return false;
}

// 总结：把 Dify query 参数按 UTF-8 做百分号编码，避免冒号、中文等字符破坏 URL。
std::wstring url_encode_component(const std::wstring& value) {
    const std::string bytes = narrow_utf8(value);
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring output;
    for (const unsigned char ch : bytes) {
        const bool unreserved =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (unreserved) {
            output.push_back(static_cast<wchar_t>(ch));
            continue;
        }
        output.push_back(L'%');
        output.push_back(hex[(ch >> 4) & 0x0F]);
        output.push_back(hex[ch & 0x0F]);
    }
    return output;
}

// 总结：执行 Dify/Cidu JSON GET 请求，历史会话等接口复用这条轻量通道。
JsonApiResponse get_json_endpoint(
    const CiduSettings& settings,
    const std::wstring& endpoint) {
    if (settings.base_url.empty()) {
        return {false, 0, L"", L"请先在设置中填写智能体地址。"};
    }
    if (settings.user.empty()) {
        return {false, 0, L"", L"当前插件缺少用户标识，请重新安装或保存一次设置。"};
    }

    URL_COMPONENTS components{};
    if (!parse_base_url(settings.base_url, components)) {
        return {false, 0, L"", L"Invalid Cidu base_url."};
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    const std::wstring path = api_path(components, endpoint);

    HINTERNET session = WinHttpOpen(
        L"CiduZW3D/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        return {false, 0, L"", L"WinHttpOpen failed."};
    }

    WinHttpSetTimeouts(
        session,
        settings.timeout_seconds * 1000,
        settings.timeout_seconds * 1000,
        settings.timeout_seconds * 1000,
        settings.timeout_seconds * 1000);

    const INTERNET_PORT port = components.nPort;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return {false, 0, L"", L"WinHttpConnect failed."};
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET http_request = WinHttpOpenRequest(
        connect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!http_request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, L"", L"WinHttpOpenRequest failed."};
    }

    std::wstring headers = L"Accept: application/json\r\n";
    if (!settings.api_key.empty()) {
        headers += L"Authorization: Bearer " + settings.api_key + L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        http_request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0);
    if (!sent || !WinHttpReceiveResponse(http_request, nullptr)) {
        WinHttpCloseHandle(http_request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, L"", L"HTTP GET request failed."};
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(
        http_request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_size,
        WINHTTP_NO_HEADER_INDEX);

    std::string response_body;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(http_request, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(http_request, chunk.data(), available, &read)) {
            break;
        }
        chunk.resize(read);
        response_body += chunk;
    }

    WinHttpCloseHandle(http_request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    const std::wstring raw = widen_utf8(response_body);
    JsonApiResponse response;
    response.ok = status_code >= 200 && status_code < 300;
    response.status_code = static_cast<long>(status_code);
    response.raw_body = raw;
    if (!response.ok) {
        response.error = raw.empty() ? L"Cidu GET request failed." : raw;
    }
    return response;
}

// 总结：处理一条 Cidu SSE data JSON，提取 answer 增量并判断是否为错误事件。
void handle_stream_data(
    const std::wstring& data,
    ChatResponse& response,
    const StreamDeltaCallback& on_delta) {
    if (data.empty() || data == L"[DONE]") {
        return;
    }

    response.raw_body += data;
    response.raw_body += L"\n";

    const std::wstring event = json_string_value(data, L"event");
    const std::wstring conversation_id = json_string_value(data, L"conversation_id");
    if (!conversation_id.empty()) {
        response.conversation_id = conversation_id;
    }
    const bool should_stream_answer = is_chat_message_event(event) && has_top_level_key(data, L"answer");
    const std::wstring delta = should_stream_answer ? json_string_value(data, L"answer") : L"";
    if (!delta.empty()) {
        response.answer += delta;
        if (on_delta) {
            on_delta(delta);
        }
    }
    if (event == L"workflow_finished" && response.answer.empty()) {
        // Dify advanced-chat 工作流有时不发送 message/agent_message 增量，
        // 只在 workflow_finished.data.outputs.answer 里给最终 answer。
        response.answer = json_string_value(data, L"answer");
    }

    if (event == L"error") {
        std::wstring message = json_string_value(data, L"message");
        if (message.empty()) {
            message = data;
        }
        response.error = message;
    }
}
#endif

}  // namespace

// 总结：保存 Cidu 客户端配置。
CiduClient::CiduClient(CiduSettings settings)
    : settings_(std::move(settings)) {}

// 总结：通过 WinHTTP 调用 Cidu `/chat-messages`，并提取 answer 字段。
ChatResponse CiduClient::send_chat_message(const ChatRequest& request) const {
    const std::wstring body = build_chat_body(settings_, request, L"blocking");
    if (settings_.base_url.empty()) {
        return {false, 0, L"", body, L"请先在设置中填写智能体地址。"};
    }

#if !defined(_WIN32)
    return {false, 0, L"", body, L"WinHTTP client is only implemented on Windows."};
#else
    URL_COMPONENTS components{};
    if (!parse_base_url(settings_.base_url, components)) {
        return {false, 0, L"", body, L"Invalid Cidu base_url."};
    }

    // 兼容 base_url 里已经带 `/v1` 的情况，最终统一拼成 `/chat-messages`。
    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    const std::wstring path = api_path(components, L"/chat-messages");

    HINTERNET session = WinHttpOpen(
        L"CiduZW3D/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        return {false, 0, L"", body, L"WinHttpOpen failed."};
    }

    // 所有阶段都使用同一个超时时间，避免 ZW3D UI 被网络请求长期卡住。
    WinHttpSetTimeouts(
        session,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000);

    const INTERNET_PORT port = components.nPort;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return {false, 0, L"", body, L"WinHttpConnect failed."};
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET http_request = WinHttpOpenRequest(
        connect,
        L"POST",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!http_request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, L"", body, L"WinHttpOpenRequest failed."};
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!settings_.api_key.empty()) {
        // Cidu API Key 使用 Bearer 方式传递；本地配置为空时跳过鉴权头。
        headers += L"Authorization: Bearer " + settings_.api_key + L"\r\n";
    }

    // Cidu API 使用 JSON/UTF-8；WinHTTP 发送的是原始字节。
    const std::string body_utf8 = narrow_utf8(body);
    const BOOL sent = WinHttpSendRequest(
        http_request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(body_utf8.data()),
        static_cast<DWORD>(body_utf8.size()),
        static_cast<DWORD>(body_utf8.size()),
        0);
    if (!sent || !WinHttpReceiveResponse(http_request, nullptr)) {
        WinHttpCloseHandle(http_request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, L"", body, L"HTTP request failed."};
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(
        http_request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_size,
        WINHTTP_NO_HEADER_INDEX);

    std::string response_body;
    DWORD available = 0;
    // 分块读取响应体，直到 WinHTTP 报告没有剩余数据。
    while (WinHttpQueryDataAvailable(http_request, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(http_request, chunk.data(), available, &read)) {
            break;
        }
        chunk.resize(read);
        response_body += chunk;
    }

    WinHttpCloseHandle(http_request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    const std::wstring raw = widen_utf8(response_body);
    ChatResponse response;
    // 这里只做轻量响应解析，业务层会继续解析 answer 里的结构化模型参数。
    response.ok = status_code >= 200 && status_code < 300;
    response.status_code = static_cast<long>(status_code);
    response.raw_body = raw;
    response.answer = json_string_value(raw, L"answer");
    response.conversation_id = json_string_value(raw, L"conversation_id");
    if (!response.ok) {
        response.error = raw.empty() ? L"Cidu request failed." : raw;
    }
    return response;
#endif
}

// 总结：通过 WinHTTP 调用 Cidu `/chat-messages` 的 SSE 流式模式，并累积完整 answer。
ChatResponse CiduClient::stream_chat_message(
    const ChatRequest& request,
    const StreamDeltaCallback& on_delta) const {
    const std::wstring body = build_chat_body(settings_, request, L"streaming");
    if (settings_.base_url.empty()) {
        return {false, 0, L"", body, L"请先在设置中填写智能体地址。"};
    }

#if !defined(_WIN32)
    (void)on_delta;
    return {false, 0, L"", body, L"WinHTTP client is only implemented on Windows."};
#else
    URL_COMPONENTS components{};
    if (!parse_base_url(settings_.base_url, components)) {
        return {false, 0, L"", body, L"Invalid Cidu base_url."};
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    const std::wstring path = api_path(components, L"/chat-messages");

    HINTERNET session = WinHttpOpen(
        L"CiduZW3D/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        return {false, 0, L"", body, L"WinHttpOpen failed."};
    }

    WinHttpSetTimeouts(
        session,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000);

    const INTERNET_PORT port = components.nPort;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return {false, 0, L"", body, L"WinHttpConnect failed."};
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET http_request = WinHttpOpenRequest(
        connect,
        L"POST",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!http_request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, L"", body, L"WinHttpOpenRequest failed."};
    }

    std::wstring headers = L"Content-Type: application/json\r\nAccept: text/event-stream\r\n";
    if (!settings_.api_key.empty()) {
        headers += L"Authorization: Bearer " + settings_.api_key + L"\r\n";
    }

    const std::string body_utf8 = narrow_utf8(body);
    const BOOL sent = WinHttpSendRequest(
        http_request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(body_utf8.data()),
        static_cast<DWORD>(body_utf8.size()),
        static_cast<DWORD>(body_utf8.size()),
        0);
    if (!sent || !WinHttpReceiveResponse(http_request, nullptr)) {
        WinHttpCloseHandle(http_request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, L"", body, L"HTTP streaming request failed."};
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(
        http_request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_size,
        WINHTTP_NO_HEADER_INDEX);

    ChatResponse response;
    response.status_code = static_cast<long>(status_code);

    std::string line_buffer;
    std::string event_data;
    DWORD available = 0;
    // SSE 是一行一行发送的；空行表示一条事件结束，这里边读边派发 answer 增量。
    while (WinHttpQueryDataAvailable(http_request, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(http_request, chunk.data(), available, &read)) {
            response.error = L"Failed to read Cidu streaming response.";
            break;
        }
        chunk.resize(read);

        for (const char ch : chunk) {
            if (ch != '\n') {
                line_buffer.push_back(ch);
                continue;
            }

            if (!line_buffer.empty() && line_buffer.back() == '\r') {
                line_buffer.pop_back();
            }
            if (line_buffer.empty()) {
                if (!event_data.empty()) {
                    handle_stream_data(widen_utf8(event_data), response, on_delta);
                    event_data.clear();
                }
            } else {
                std::string value;
                if (sse_data_value(line_buffer, value)) {
                    if (!event_data.empty()) {
                        event_data.push_back('\n');
                    }
                    event_data += value;
                }
            }
            line_buffer.clear();
        }
    }

    if (!line_buffer.empty()) {
        std::string value;
        if (sse_data_value(line_buffer, value)) {
            if (!event_data.empty()) {
                event_data.push_back('\n');
            }
            event_data += value;
        }
    }
    if (!event_data.empty()) {
        handle_stream_data(widen_utf8(event_data), response, on_delta);
    }

    WinHttpCloseHandle(http_request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    response.ok = status_code >= 200 && status_code < 300 && response.error.empty();
    if (!response.ok && response.error.empty()) {
        response.error = response.raw_body.empty() ? L"Cidu streaming request failed." : response.raw_body;
    }
    return response;
#endif
}

// 总结：通过 WinHTTP 调用 Cidu `/files/upload`，并提取后续聊天可引用的文件 ID。
FileUploadResponse CiduClient::upload_file(const std::filesystem::path& path) const {
#if !defined(_WIN32)
    (void)path;
    return {false, 0, {}, L"", L"WinHTTP client is only implemented on Windows."};
#else
    if (settings_.base_url.empty()) {
        return {false, 0, {}, L"", L"请先在设置中填写智能体地址。"};
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {false, 0, {}, L"", L"File could not be opened: " + path.wstring()};
    }
    const std::string file_bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::wstring file_name_wide = path.filename().wstring();
    const std::string file_name = narrow_utf8(file_name_wide.empty() ? L"upload.bin" : file_name_wide);

    URL_COMPONENTS components{};
    if (!parse_base_url(settings_.base_url, components)) {
        return {false, 0, {}, L"", L"Invalid Cidu base_url."};
    }

    const std::string boundary = "----CiduZW3DFileBoundary7MA4YWxkTrZu0gW";
    std::string body;
    // multipart 字段名按 Cidu 兼容协议固定为 user 和 file。
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"user\"\r\n\r\n";
    body += narrow_utf8(settings_.user) + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + file_name + "\"\r\n";
    body += "Content-Type: application/octet-stream\r\n\r\n";
    body += file_bytes;
    body += "\r\n--" + boundary + "--\r\n";

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    const std::wstring request_path = api_path(components, L"/files/upload");
    HINTERNET session = WinHttpOpen(
        L"CiduZW3D/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        return {false, 0, {}, L"", L"WinHttpOpen failed."};
    }

    WinHttpSetTimeouts(
        session,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000,
        settings_.timeout_seconds * 1000);

    const INTERNET_PORT port = components.nPort;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return {false, 0, {}, L"", L"WinHttpConnect failed."};
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET http_request = WinHttpOpenRequest(
        connect,
        L"POST",
        request_path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);
    if (!http_request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, {}, L"", L"WinHttpOpenRequest failed."};
    }

    std::wstring headers = L"Content-Type: multipart/form-data; boundary=" + widen_utf8(boundary) + L"\r\n";
    if (!settings_.api_key.empty()) {
        headers += L"Authorization: Bearer " + settings_.api_key + L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        http_request,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
    if (!sent || !WinHttpReceiveResponse(http_request, nullptr)) {
        WinHttpCloseHandle(http_request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return {false, 0, {}, L"", L"HTTP file upload failed."};
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(
        http_request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_size,
        WINHTTP_NO_HEADER_INDEX);

    std::string response_body;
    DWORD available = 0;
    // 上传响应通常很小，但仍按 WinHTTP 推荐方式分块读取。
    while (WinHttpQueryDataAvailable(http_request, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(http_request, chunk.data(), available, &read)) {
            break;
        }
        chunk.resize(read);
        response_body += chunk;
    }

    WinHttpCloseHandle(http_request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    const std::wstring raw = widen_utf8(response_body);
    FileUploadResponse response;
    response.status_code = static_cast<long>(status_code);
    response.raw_body = raw;
    response.file.id = json_string_value(raw, L"id");
    response.file.name = json_string_value(raw, L"name");
    if (response.file.name.empty()) {
        response.file.name = file_name_wide;
    }
    response.ok = status_code >= 200 && status_code < 300 && !response.file.id.empty();
    if (!response.ok) {
        response.error = raw.empty() ? L"Cidu file upload failed." : raw;
    }
    return response;
#endif
}

// 总结：按当前 user 从 Dify 拉取历史会话列表。
JsonApiResponse CiduClient::list_conversations(std::size_t limit) const {
#if !defined(_WIN32)
    (void)limit;
    return {false, 0, L"", L"WinHTTP client is only implemented on Windows."};
#else
    if (limit == 0) {
        limit = 50;
    }
    const std::wstring endpoint =
        L"/conversations?user=" + url_encode_component(settings_.user) +
        L"&limit=" + std::to_wstring(limit) +
        L"&sort_by=-updated_at";
    return get_json_endpoint(settings_, endpoint);
#endif
}

// 总结：按当前 user 和 conversation_id 从 Dify 拉取会话消息。
JsonApiResponse CiduClient::list_messages(
    const std::wstring& conversation_id,
    std::size_t limit) const {
#if !defined(_WIN32)
    (void)conversation_id;
    (void)limit;
    return {false, 0, L"", L"WinHTTP client is only implemented on Windows."};
#else
    if (conversation_id.empty()) {
        return {false, 0, L"", L"缺少 Dify 会话 ID。"};
    }
    if (limit == 0) {
        limit = 100;
    }
    const std::wstring endpoint =
        L"/messages?user=" + url_encode_component(settings_.user) +
        L"&conversation_id=" + url_encode_component(conversation_id) +
        L"&limit=" + std::to_wstring(limit);
    return get_json_endpoint(settings_, endpoint);
#endif
}

}  // namespace cidu_zw3d
