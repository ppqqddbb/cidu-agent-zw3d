#include "agent_service.h"

#include "jiavu_automation.h"
#include "parameter_value_bridge.h"

#include "cidu_zw3d/export.h"
#include "cidu_zw3d/json_protocol.h"
#include "cidu_zw3d/plugin_config.h"
#include "cidu_zw3d/zw3d_host.h"

#include <cstdio>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cwctype>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(CIDU_ZW3D_WITH_REAL_SDK)
#include "zwapi_cmd.h"
#include "zwapi_ui_dockwidget.h"
#include "zwapi_ui_form.h"
#include "zwapi_userinput.h"
#endif

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
#include <WebView2.h>
#include <wrl.h>
#endif
#endif

namespace {

#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
#endif

constexpr char kAgentFormName[] = "CiduZW3DAgentForm";
constexpr int kPromptFieldId = 1;
constexpr int kResultFieldId = 2;
constexpr int kActionButtonFieldId = 3;
constexpr int kAttachmentFieldId = 4;

// ZW3D 宿主封装实例，负责注册命令、显示消息以及承接建模入口。
cidu_zw3d::Zw3DHost g_host;
// Agent 服务生命周期跟随插件 DLL，初始化后供按钮和命令复用。
std::unique_ptr<cidu_zw3d::AgentService> g_service;
// 导出函数返回 const wchar_t*，这里用静态缓存保证调用方读取时指针仍有效。
std::wstring g_last_result;
// 侧边栏只需要在 ZW3D 完成启动后注册一次，避免重复插入页签。
bool g_sidebar_registered = false;
// 当前侧边栏选择的附件路径，提交时会先上传到 Cidu 服务端。
std::vector<std::filesystem::path> g_selected_files;
// Web 前端一次只允许提交一个 Agent 请求，避免多次流式输出互相交错。
std::atomic_bool g_agent_request_running{false};
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
// WebView2 环境对象，负责创建和复用侧边栏里的浏览器控件。
ComPtr<ICoreWebView2Environment> g_webview_environment;
// WebView2 控制器，负责把浏览器控件嵌入 ZW3D 表单窗口。
ComPtr<ICoreWebView2Controller> g_webview_controller;
// WebView2 页面对象，用于收发前端消息。
ComPtr<ICoreWebView2> g_webview;
// WebView2 控件挂载的 ZW3D 表单窗口。
HWND g_webview_parent = nullptr;
// ZW3D 表单原始窗口过程，子类化后用于在关闭表单前释放 WebView2。
WNDPROC g_webview_parent_original_proc = nullptr;
// WebView2 控件实际承载窗口，由插件创建并铺满 ZW3D 表单客户区。
HWND g_webview_host = nullptr;
// WebView2 初始化失败时显示在表单里的诊断文本，避免用户只看到空白窗。
HWND g_webview_fallback = nullptr;
// WebView2 自动重铺定时器 ID，用于跟随 ZW3D 侧边栏尺寸变化。
constexpr UINT_PTR kWebViewResizeTimerId = 1001;
// 后台线程通过该消息把 JSON 投递回 WebView2 所在线程。
constexpr UINT kWebViewPostJsonMessage = WM_APP + 173;
// 后台线程通过该消息把佳鱼参数导入命令投递回 ZW3D/UI 线程执行。
constexpr UINT kRunJiavuParameterImportMessage = WM_APP + 174;
// 记录上次铺设尺寸，避免尺寸未变化时重复调整和刷日志。
int g_webview_last_width = 0;
int g_webview_last_height = 0;
// WebView2 消息回调注册令牌，供后续必要时解除绑定。
EventRegistrationToken g_web_message_token = {};
// 插件自行初始化 COM 时记录状态，卸载时成对释放。
bool g_com_initialized = false;
#endif

// 总结：提前声明导出函数，供命令回调和表单回调复用。
extern "C" CIDU_ZW3D_API const wchar_t* CiduZW3DAskAndGenerate(const wchar_t* prompt);
// 总结：提前声明初始化函数，供 WebView2 消息回调在服务未就绪时自恢复。
extern "C" CIDU_ZW3D_API int CiduZW3DAgentInitialize();
// 总结：提前声明文件选择函数，供原生表单和 WebView2 前端共用。
bool choose_upload_file(std::filesystem::path& path);
// 总结：提前声明显示函数，供 Ribbon 和命令入口复用。
int show_agent_form();
// 总结：提前声明佳鱼参数导入命令调用，供 WebView 消息和降级路径复用。
void send_jiavu_parameter_import_command();
// 总结：提前声明日志函数，供窗口入口辅助函数记录诊断信息。
void append_debug_log(const std::string& message);

// 总结：安全写入 ZW3D SDK 使用的定长 char 数组，避免侧边栏注册信息越界。
template <std::size_t Size>
void copy_sdk_text(char (&target)[Size], const char* value) {
    std::snprintf(target, Size, "%s", value ? value : "");
}

// 总结：向插件目录写入轻量调试日志，用于判断侧边栏 API 是否被 ZW3D 接受。
void append_debug_log(const std::string& message) {
    std::ofstream log(cidu_zw3d::module_directory() / L"cidu_agent_debug.log", std::ios::app);
    if (log) {
        log << message << '\n';
    }
}

// 总结：返回旧版本保存在插件同目录下的 Cidu 配置文件路径。
std::filesystem::path installed_config_path() {
    return cidu_zw3d::module_directory() / L"cidu_zw3d_config.json";
}

std::filesystem::path config_path_from_env(
    const wchar_t* variable,
    const std::filesystem::path& relative_path) {
#if defined(_WIN32)
    const DWORD size = GetEnvironmentVariableW(variable, nullptr, 0);
    if (size > 0) {
        std::wstring value(size, L'\0');
        const DWORD written = GetEnvironmentVariableW(variable, value.data(), size);
        if (written > 0 && written < size) {
            value.resize(written);
            return std::filesystem::path(value) / relative_path;
        }
    }
#else
    (void)variable;
    (void)relative_path;
#endif
    return {};
}

void append_unique_config_path(
    std::vector<std::filesystem::path>& paths,
    const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(path);
    }
}

std::filesystem::path roaming_config_path() {
    return config_path_from_env(L"APPDATA", std::filesystem::path(L"CiduZW3D") / L"cidu_zw3d_config.json");
}

std::filesystem::path local_config_path() {
    return config_path_from_env(L"LOCALAPPDATA", std::filesystem::path(L"CiduZW3D") / L"cidu_zw3d_config.json");
}

std::filesystem::path profile_config_path() {
    return config_path_from_env(L"USERPROFILE", std::filesystem::path(L".CiduZW3D") / L"cidu_zw3d_config.json");
}

// 总结：返回当前用户可写的首选 Cidu 配置文件路径。
std::filesystem::path user_config_path() {
    const std::filesystem::path roaming_path = roaming_config_path();
    if (!roaming_path.empty()) {
        return roaming_path;
    }
    const std::filesystem::path local_path = local_config_path();
    if (!local_path.empty()) {
        return local_path;
    }
    const std::filesystem::path profile_path = profile_config_path();
    if (!profile_path.empty()) {
        return profile_path;
    }
    return installed_config_path();
}

std::vector<std::filesystem::path> config_read_paths() {
    std::vector<std::filesystem::path> paths;
    append_unique_config_path(paths, roaming_config_path());
    append_unique_config_path(paths, local_config_path());
    append_unique_config_path(paths, profile_config_path());
    append_unique_config_path(paths, installed_config_path());
    append_unique_config_path(paths, user_config_path());
    return paths;
}

std::filesystem::path config_path();

std::vector<std::filesystem::path> config_save_paths() {
    std::vector<std::filesystem::path> paths;
    append_unique_config_path(paths, config_path());
    append_unique_config_path(paths, installed_config_path());
    append_unique_config_path(paths, roaming_config_path());
    append_unique_config_path(paths, local_config_path());
    append_unique_config_path(paths, profile_config_path());
    append_unique_config_path(paths, user_config_path());
    return paths;
}

// 总结：读取时优先用用户配置；没有用户配置时兼容旧版本安装目录配置。
std::filesystem::path config_path() {
    for (const auto& path : config_read_paths()) {
        std::error_code error;
        if (std::filesystem::exists(path, error) && !error) {
            return path;
        }
    }
    return user_config_path();
}

bool save_settings_with_fallback(
    const cidu_zw3d::CiduSettings& settings,
    std::wstring& error_message) {
    for (const auto& path : config_save_paths()) {
        std::wstring candidate_error;
        if (cidu_zw3d::save_cidu_settings(path, settings, &candidate_error)) {
            return true;
        }
        if (!candidate_error.empty()) {
            error_message = candidate_error;
        }
    }
    if (error_message.empty()) {
        error_message = L"没有可用的配置文件路径。";
    }
    return false;
}

std::wstring new_machine_user_id() {
#if defined(_WIN32)
    GUID guid{};
    if (SUCCEEDED(CoCreateGuid(&guid))) {
        wchar_t buffer[40]{};
        const int length = StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
        if (length > 2) {
            std::wstring value(buffer);
            if (!value.empty() && value.front() == L'{') {
                value.erase(value.begin());
            }
            if (!value.empty() && value.back() == L'}') {
                value.pop_back();
            }
            return L"cidu-zw3d:" + value;
        }
    }
#endif
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return L"cidu-zw3d:" + std::to_wstring(now);
}

bool needs_machine_user_id(const std::wstring& user) {
    return user.empty() || user == L"zw3d-user";
}

// 总结：旧包使用固定 zw3d-user，会让所有机器共享 Dify 历史；这里自动迁移成机器 UUID。
cidu_zw3d::CiduSettings load_settings_with_machine_user() {
    cidu_zw3d::CiduSettings settings = cidu_zw3d::load_cidu_settings(config_path());
    if (!needs_machine_user_id(settings.user)) {
        return settings;
    }
    settings.user = new_machine_user_id();
    std::wstring save_error;
    if (!save_settings_with_fallback(settings, save_error)) {
        append_debug_log("load_settings_with_machine_user: failed to save generated user id");
    }
    return settings;
}

// 总结：返回 WebView2 前端页面文件路径，安装后与插件 DLL 位于同一目录。
std::filesystem::path web_panel_path() {
    return cidu_zw3d::module_directory() / L"web" / L"agent_panel.html";
}

// 总结：返回佳鱼知识库/图谱同步工具路径，安装后与插件 DLL 位于同一目录。
std::filesystem::path knowledge_sync_tool_path() {
#if defined(_WIN32)
    return cidu_zw3d::module_directory() / L"CiduZW3D" / L"bin" / L"JiavuKnowledgeSync.exe";
#else
    return cidu_zw3d::module_directory() / L"CiduZW3D" / L"bin" / L"JiavuKnowledgeSync";
#endif
}

// 总结：返回本地知识库和知识图谱输出目录。
std::filesystem::path knowledge_output_path() {
    return cidu_zw3d::module_directory() / L"knowledge";
}

// 总结：按当前配置文件重建 Agent 服务，让设置保存后立即生效。
bool reload_agent_service() {
    const cidu_zw3d::CiduSettings settings = load_settings_with_machine_user();
    auto service = std::make_unique<cidu_zw3d::AgentService>(cidu_zw3d::CiduClient(settings), g_host);
    if (!service->initialize()) {
        return false;
    }
    g_service = std::move(service);
    return true;
}

// 总结：返回随前端一起安装的 marked 浏览器版解析器路径。
std::filesystem::path marked_script_path() {
    return cidu_zw3d::module_directory() / L"web" / L"vendor" / L"marked.umd.js";
}

std::filesystem::path panel_style_path() {
    return cidu_zw3d::module_directory() / L"web" / L"agent_panel.css";
}

std::vector<std::filesystem::path> panel_script_paths() {
    const std::filesystem::path web_dir = cidu_zw3d::module_directory() / L"web";
    return {
        web_dir / L"agent_panel_bootstrap.js",
        web_dir / L"agent_panel_chat.js",
        web_dir / L"agent_panel_parameters.js",
        web_dir / L"agent_panel_equipment_mapping.js",
        web_dir / L"agent_panel_parameter_bridge.js",
        web_dir / L"agent_panel_auto_recalculation.js",
        web_dir / L"agent_panel_jiavu.js",
        web_dir / L"agent_panel_events.js",
        web_dir / L"agent_panel_param_table.js"
    };
}

// 总结：把 ZW3D 命令或表单传入的窄字符串转换为内部宽字符串。
std::wstring widen_prompt(const char* value) {
    if (!value || !*value) {
        return L"";
    }
#if defined(_WIN32)
    // 优先按严格 UTF-8 解析，失败后再使用系统本地编码以兼容中文 UI 输入。
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    UINT code_page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (size <= 1) {
        code_page = CP_ACP;
        flags = 0;
        size = MultiByteToWideChar(code_page, flags, value, -1, nullptr, 0);
    }
    std::wstring text(static_cast<std::size_t>(size > 0 ? size - 1 : 0), L'\0');
    if (!text.empty()) {
        MultiByteToWideChar(code_page, flags, value, -1, text.data(), size);
    }
    return text;
#else
    std::string text(value);
    return std::wstring(text.begin(), text.end());
#endif
}

// 总结：把内部宽字符串转换成 ZW3D 表单控件可显示的本地编码文本。
std::string narrow_text(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
#if defined(_WIN32)
    int size = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string text(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, text.data(), size, nullptr, nullptr);
    return text;
#else
    return std::string(value.begin(), value.end());
#endif
}

// 总结：把宽字符串转换成 WebView2 消息通道使用的 UTF-8 字节串。
std::string narrow_utf8_text(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string text(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, text.data(), size, nullptr, nullptr);
    return text;
#else
    return std::string(value.begin(), value.end());
#endif
}

#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
std::string hresult_hex(HRESULT value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << static_cast<unsigned long>(value);
    return stream.str();
}

std::wstring hresult_hex_wide(HRESULT value) {
    const std::string text = hresult_hex(value);
    return std::wstring(text.begin(), text.end());
}

void show_webview_fallback(const std::wstring& message) {
    HWND parent = g_webview_host ? g_webview_host : g_webview_parent;
    if (!parent || !IsWindow(parent)) {
        append_debug_log("webview_fallback: no parent window");
        return;
    }
    if (!g_webview_fallback || !IsWindow(g_webview_fallback) || GetParent(g_webview_fallback) != parent) {
        g_webview_fallback = CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            16,
            16,
            400,
            160,
            parent,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        SendMessageW(g_webview_fallback, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    }
    SetWindowTextW(g_webview_fallback, message.c_str());
    RECT bounds = {};
    GetClientRect(parent, &bounds);
    const int fallback_width = (bounds.right - bounds.left - 32) > 100 ? (bounds.right - bounds.left - 32) : 100;
    const int fallback_height = (bounds.bottom - bounds.top - 32) > 80 ? (bounds.bottom - bounds.top - 32) : 80;
    SetWindowPos(
        g_webview_fallback,
        HWND_TOP,
        16,
        16,
        fallback_width,
        fallback_height,
        SWP_SHOWWINDOW);
}

void hide_webview_fallback() {
    if (g_webview_fallback && IsWindow(g_webview_fallback)) {
        ShowWindow(g_webview_fallback, SW_HIDE);
    }
}
#endif

// 总结：读取本地文本文件，用于把前端 HTML 注入 WebView2。
std::wstring read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return L"";
    }
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
#if defined(_WIN32)
    if (bytes.empty()) {
        return L"";
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), size);
    return text;
#else
    return std::wstring(bytes.begin(), bytes.end());
#endif
}

// 总结：解析同步工具输出中的布尔字段。
bool json_bool_value(const std::wstring& json, const std::wstring& key) {
    const std::wstring quoted_key = L"\"" + key + L"\"";
    const auto key_pos = json.find(quoted_key);
    if (key_pos == std::wstring::npos) {
        return false;
    }
    const auto colon_pos = json.find(L':', key_pos + quoted_key.size());
    if (colon_pos == std::wstring::npos) {
        return false;
    }
    auto value_pos = colon_pos + 1;
    while (value_pos < json.size() && std::iswspace(json[value_pos])) {
        ++value_pos;
    }
    return json.compare(value_pos, 4, L"true") == 0;
}

void replace_placeholder_once(
    std::wstring& html,
    const std::wstring& placeholder,
    const std::wstring& replacement) {
    const std::size_t position = html.find(placeholder);
    if (position != std::wstring::npos) {
        html.replace(position, placeholder.size(), replacement);
    }
}

// 总结：WebView2 使用 NavigateToString 加载页面，外部路径不稳定，因此启动时内联前端资源。
std::wstring compose_web_panel_html(std::wstring html) {
    const std::wstring style = read_text_file(panel_style_path());
    if (style.empty()) {
        append_debug_log("initialize_webview: agent_panel.css is empty or missing");
        return html;
    }
    std::wstring panel_script;
    for (const auto& path : panel_script_paths()) {
        const std::wstring part = read_text_file(path);
        if (part.empty()) {
            append_debug_log("initialize_webview: panel script file is empty or missing");
            return html;
        }
        panel_script += part;
        panel_script += L"\n";
    }
    const std::wstring marked = read_text_file(marked_script_path());
    if (marked.empty()) {
        append_debug_log("initialize_webview: marked.umd.js is empty or missing");
        return html;
    }

    replace_placeholder_once(html, L"<!-- CIDU_MARKED_SCRIPT -->", L"<script>\n" + marked + L"\n</script>");
    replace_placeholder_once(html, L"<!-- CIDU_PANEL_STYLE -->", L"<style>\n" + style + L"\n</style>");
    replace_placeholder_once(html, L"<!-- CIDU_PANEL_SCRIPT -->", L"<script>\n" + panel_script + L"\n</script>");
    return html;
}

// 总结：把 JSON 对象里的字符串键值对转换成适合聊天窗口阅读的多行说明。
std::wstring format_string_object(
    const std::map<std::wstring, std::wstring>& values,
    const std::wstring& empty_text) {
    if (values.empty()) {
        return empty_text;
    }

    std::wstring text;
    for (const auto& [key, value] : values) {
        text += L"- ";
        text += key;
        if (!value.empty()) {
            text += L": ";
            text += value;
        }
        text += L"\n";
    }
    if (!text.empty()) {
        text.pop_back();
    }
    return text;
}

// 总结：把数字参数对象转换成可读清单，用于展示已识别的实际数值。
std::wstring format_number_object(
    const std::map<std::wstring, double>& values,
    const std::wstring& empty_text) {
    if (values.empty()) {
        return empty_text;
    }

    std::wostringstream out;
    bool first = true;
    for (const auto& [key, value] : values) {
        if (!first) {
            out << L"\n";
        }
        out << L"- " << key << L": " << value;
        first = false;
    }
    return out.str();
}

// 总结：把当前附件选择整理成表单状态栏可显示的短文本。
std::wstring selected_file_summary() {
    if (g_selected_files.empty()) {
        return L"未选择附件";
    }

    const auto& path = g_selected_files.front();
    const std::wstring name = path.filename().wstring();
    return L"已选择：" + (name.empty() ? path.wstring() : name);
}

// 总结：把当前附件选择整理成发送前聊天记录里可读的说明。
std::wstring selected_file_chat_note() {
    if (g_selected_files.empty()) {
        return L"";
    }

    std::wstring text = L"\n\n附件\n";
    for (const auto& path : g_selected_files) {
        const std::wstring name = path.filename().wstring();
        text += L"- ";
        text += name.empty() ? path.wstring() : name;
        text += L"\n";
    }
    if (!text.empty()) {
        text.pop_back();
    }
    return text;
}

// 总结：把当前附件选择整理成 Web 前端可显示的 JSON 数组。
// size 字段供前端气泡里渲染 "EXT · 1.60 MB"；取不到时（文件已删除/权限不足）写 0，前端只省略大小行。
std::wstring selected_files_json() {
    std::wstring json = L"[";
    bool first = true;
    for (const auto& path : g_selected_files) {
        if (!first) {
            json += L",";
        }
        const std::wstring name = path.filename().wstring();
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        const std::uintmax_t safe_size = ec ? 0 : size;
        json += L"{\"name\":\"" + cidu_zw3d::json_escape(name.empty() ? path.wstring() : name) + L"\",";
        json += L"\"path\":\"" + cidu_zw3d::json_escape(path.wstring()) + L"\",";
        json += L"\"size\":" + std::to_wstring(safe_size) + L"}";
        first = false;
    }
    json += L"]";
    return json;
}

// 总结：把 AgentRunResult 统一组装成插件对外返回和界面解析共用的 JSON。
std::wstring format_agent_result_json(const cidu_zw3d::AgentRunResult& result) {
    std::wstring json = L"{\"ok\":";
    json += result.ok ? L"true" : L"false";
    json += L",\"message\":\"" + cidu_zw3d::json_escape(result.message) + L"\"";
    if (!result.answer_markdown.empty()) {
        json += L",\"answer_markdown\":\"" + cidu_zw3d::json_escape(result.answer_markdown) + L"\"";
    }
    if (!result.selection.selected_model.empty()) {
        json += L",\"selected_model\":\"" + cidu_zw3d::json_escape(result.selection.selected_model) + L"\"";
    }
    if (!result.selection.template_name.empty()) {
        json += L",\"selection_template_name\":\"" + cidu_zw3d::json_escape(result.selection.template_name) + L"\"";
    }
    if (!result.selection.full_node_path.empty()) {
        json += L",\"selected_full_node_path\":\"" + cidu_zw3d::json_escape(result.selection.full_node_path) + L"\"";
    }
    if (!result.selection.model_description.empty()) {
        json += L",\"model_description\":\"" + cidu_zw3d::json_escape(result.selection.model_description) + L"\"";
    }
    if (!result.selection.parameter_requirements.empty()) {
        json += L",\"parameter_requirements\":{";
        bool first = true;
        // parameter_requirements 是界面展示字段，不会传给参数化插件执行建模。
        for (const auto& [key, value] : result.selection.parameter_requirements) {
            if (!first) {
                json += L",";
            }
            json += L"\"" + cidu_zw3d::json_escape(key) + L"\":\"" + cidu_zw3d::json_escape(value) + L"\"";
            first = false;
        }
        json += L"}";
    }
    if (!result.parameters.template_name.empty()) {
        json += L",\"template_name\":\"" + cidu_zw3d::json_escape(result.parameters.template_name) + L"\"";
    }
    if (!result.parameters.numeric.empty()) {
        json += L",\"numeric\":{";
        bool first = true;
        // numeric/text 与 template_name 共同组成后续参数化插件实际消费的参数协议。
        for (const auto& [key, value] : result.parameters.numeric) {
            if (!first) {
                json += L",";
            }
            std::wostringstream value_text;
            value_text << value;
            json += L"\"" + cidu_zw3d::json_escape(key) + L"\":" + value_text.str();
            first = false;
        }
        json += L"}";
    }
    if (!result.parameters.text.empty()) {
        json += L",\"text\":{";
        bool first = true;
        for (const auto& [key, value] : result.parameters.text) {
            if (!first) {
                json += L",";
            }
            json += L"\"" + cidu_zw3d::json_escape(key) + L"\":\"" + cidu_zw3d::json_escape(value) + L"\"";
            first = false;
        }
        json += L"}";
    }
    if (!result.created_file.empty()) {
        json += L",\"created_file\":\"" + cidu_zw3d::json_escape(result.created_file) + L"\"";
    }
    if (!result.conversation_id.empty()) {
        json += L",\"conversation_id\":\"" + cidu_zw3d::json_escape(result.conversation_id) + L"\"";
    }
    json += L"}";
    return json;
}

// 总结：执行一次 Agent 请求，并返回前端和导出函数共用的 JSON 结果。
std::wstring run_agent_request(const std::wstring& prompt, const std::vector<std::filesystem::path>& files) {
    if (!g_service) {
        CiduZW3DAgentInitialize();
    }
    const auto result = g_service->ask_and_generate(prompt, files);
    return format_agent_result_json(result);
}

// 总结：执行一次指定 Cidu 会话内的 Agent 请求，并返回前端可解析的 JSON 结果。
std::wstring run_agent_request(
    const std::wstring& prompt,
    const std::vector<std::filesystem::path>& files,
    const std::wstring& conversation_id) {
    if (!g_service) {
        CiduZW3DAgentInitialize();
    }
    const auto result = g_service->ask_and_generate(prompt, files, conversation_id);
    return format_agent_result_json(result);
}

// 总结：提前声明 WebView2 投递工具，供后台流式请求复用。
void queue_webview_json_message(const std::wstring& json);
// 总结：提前声明 WebView2 消息组装工具，默认只带 type 字段。
std::wstring webview_json_message(const std::wstring& type, const std::wstring& payload_fields = L"");

// 总结：在后台线程执行流式 Agent 请求，并把增量和最终结果投递给 WebView2。
void run_agent_request_streaming_async(
    const std::wstring& prompt,
    const std::vector<std::filesystem::path>& files,
    const std::wstring& conversation_id) {
    if (g_agent_request_running.exchange(true)) {
        queue_webview_json_message(webview_json_message(
            L"status",
            L"\"message\":\"" + cidu_zw3d::json_escape(L"已有请求正在执行，请稍等。") + L"\""));
        return;
    }

    std::thread([prompt, files, conversation_id]() {
        queue_webview_json_message(webview_json_message(L"streamStart"));
        queue_webview_json_message(webview_json_message(
            L"status",
            L"\"message\":\"" + cidu_zw3d::json_escape(L"正在连接 Cidu 并等待流式输出...") + L"\""));

        if (!g_service) {
            queue_webview_json_message(webview_json_message(
                L"status",
                L"\"message\":\"" + cidu_zw3d::json_escape(L"cidu-agent 服务未初始化。") + L"\""));
            queue_webview_json_message(webview_json_message(L"streamEnd"));
            g_agent_request_running.store(false);
            return;
        }

        const auto result = g_service->ask_and_generate_streaming(
            prompt,
            files,
            conversation_id,
            [](const std::wstring& delta) {
                queue_webview_json_message(webview_json_message(
                    L"streamDelta",
                    L"\"delta\":\"" + cidu_zw3d::json_escape(delta) + L"\""));
            });

        const std::wstring result_json = format_agent_result_json(result);
        queue_webview_json_message(webview_json_message(L"streamEnd"));
        queue_webview_json_message(
            L"{\"type\":\"result\",\"payload\":" + result_json + L"}");
        g_agent_request_running.store(false);
    }).detach();
}

// 总结：把当前附件列表同步给 WebView2 前端页面。
void post_attachments_to_webview() {
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (!g_webview) {
        return;
    }
    const std::wstring message = L"{\"type\":\"attachments\",\"files\":" + selected_files_json() + L"}";
    g_webview->PostWebMessageAsJson(message.c_str());
#endif
}

// 总结：把 JSON 消息投递回 WebView2 所在线程，避免后台线程直接操作 UI 对象。
void queue_webview_json_message(const std::wstring& json) {
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (!g_webview_host) {
        return;
    }
    auto* message = new std::wstring(json);
    if (!PostMessageW(g_webview_host, kWebViewPostJsonMessage, 0, reinterpret_cast<LPARAM>(message))) {
        delete message;
    }
#else
    (void)json;
#endif
}

bool queue_jiavu_parameter_import_command() {
#if defined(CIDU_ZW3D_WITH_REAL_SDK) && defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (!g_webview_host || !PostMessageW(g_webview_host, kRunJiavuParameterImportMessage, 0, 0)) {
        append_debug_log("JYParameterImport: failed to post UI-thread command");
        return false;
    }
    append_debug_log("JYParameterImport: posted UI-thread command");
    return true;
#elif defined(CIDU_ZW3D_WITH_REAL_SDK)
    append_debug_log("JYParameterImport: fallback direct cvxCmdSend");
    send_jiavu_parameter_import_command();
    return true;
#else
    return true;
#endif
}

// 总结：组装通用 WebView2 JSON 消息，适合后台线程安全投递。
std::wstring webview_json_message(const std::wstring& type, const std::wstring& payload_fields) {
    std::wstring message = L"{\"type\":\"" + cidu_zw3d::json_escape(type) + L"\"";
    if (!payload_fields.empty()) {
        message += L",";
        message += payload_fields;
    }
    message += L"}";
    return message;
}

// 总结：把 Agent 执行结果同步给 WebView2 前端页面。
void post_result_to_webview(const std::wstring& result_json) {
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (!g_webview) {
        return;
    }
    const std::wstring message = L"{\"type\":\"result\",\"payload\":" + result_json + L"}";
    g_webview->PostWebMessageAsJson(message.c_str());
#else
    (void)result_json;
#endif
}

// 总结：把 WebView2 前端页面需要显示的状态消息发回页面。
void post_status_to_webview(const std::wstring& status) {
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (!g_webview) {
        return;
    }
    const std::wstring message =
        L"{\"type\":\"status\",\"message\":\"" + cidu_zw3d::json_escape(status) + L"\"}";
    g_webview->PostWebMessageAsJson(message.c_str());
#else
    (void)status;
#endif
}

// 总结：把当前服务地址和 API Key 发送给 Web 设置面板。
void post_settings_to_webview() {
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (!g_webview) {
        return;
    }
    const cidu_zw3d::CiduSettings settings = load_settings_with_machine_user();
    const std::wstring message =
        L"{\"type\":\"settings\",\"base_url\":\"" + cidu_zw3d::json_escape(settings.base_url) +
        L"\",\"api_key\":\"" + cidu_zw3d::json_escape(settings.api_key) +
        L"\",\"jiavu_db_host\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_host) +
        L"\",\"jiavu_db_port\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_port) +
        L"\",\"jiavu_db_name\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_name) +
        L"\",\"jiavu_db_user\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_user) +
        L"\",\"jiavu_db_password\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_password) +
        L"\",\"dify_sync_base_url\":\"" + cidu_zw3d::json_escape(settings.dify_sync_base_url) +
        L"\",\"dify_dataset_api_key\":\"" + cidu_zw3d::json_escape(settings.dify_dataset_api_key) +
        L"\",\"dify_dataset_id\":\"" + cidu_zw3d::json_escape(settings.dify_dataset_id) +
        L"\",\"dify_knowledge_graph_id\":\"" + cidu_zw3d::json_escape(settings.dify_knowledge_graph_id) +
        L"\",\"dify_dataset_indexing_technique\":\"" +
            cidu_zw3d::json_escape(settings.dify_dataset_indexing_technique) +
        L"\",\"dify_knowledge_base_document_name\":\"" +
            cidu_zw3d::json_escape(settings.dify_knowledge_base_document_name) +
        L"\",\"jiavu_model_excel_dir\":\"" + cidu_zw3d::json_escape(settings.jiavu_model_excel_dir) +
        L"\",\"jyasm_service_base_url\":\"" + cidu_zw3d::json_escape(settings.jyasm_service_base_url) +
        L"\",\"jyasm_service_token\":\"" + cidu_zw3d::json_escape(settings.jyasm_service_token) +
        L"\",\"jyasm_service_public_base_url\":\"" +
            cidu_zw3d::json_escape(settings.jyasm_service_public_base_url) +
        L"\",\"jyasm_service_trigger_refresh\":\"" +
            cidu_zw3d::json_escape(settings.jyasm_service_trigger_refresh) +
        L"\",\"jyasm_service_timeout_ms\":\"" +
            cidu_zw3d::json_escape(settings.jyasm_service_timeout_ms) + L"\"}";
    g_webview->PostWebMessageAsJson(message.c_str());
#endif
}

std::wstring json_payload_or_null(const std::wstring& raw) {
    if (raw.empty()) {
        return L"null";
    }
    const auto first = raw.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"null";
    }
    const wchar_t ch = raw[first];
    return (ch == L'{' || ch == L'[') ? raw : L"null";
}

std::wstring dify_history_error_message(const cidu_zw3d::JsonApiResponse& response) {
    if (!response.error.empty()) {
        return response.error;
    }
    if (!response.raw_body.empty() && json_payload_or_null(response.raw_body) == L"null") {
        return response.raw_body;
    }
    return L"";
}

void post_dify_history_response(
    const std::wstring& type,
    const cidu_zw3d::JsonApiResponse& response,
    const std::wstring& extra_fields = L"") {
    queue_webview_json_message(
        L"{\"type\":\"" + cidu_zw3d::json_escape(type) +
        L"\",\"ok\":" + std::wstring(response.ok ? L"true" : L"false") +
        L",\"status_code\":" + std::to_wstring(response.status_code) +
        L",\"payload\":" + json_payload_or_null(response.raw_body) +
        L",\"message\":\"" + cidu_zw3d::json_escape(dify_history_error_message(response)) + L"\"" +
        (extra_fields.empty() ? L"" : L"," + extra_fields) + L"}");
}

// 总结：从 Dify 按当前机器 user 拉取历史会话列表，避免只依赖本地内存/缓存。
void run_dify_conversations_async() {
    std::thread([]() {
        const auto settings = load_settings_with_machine_user();
        cidu_zw3d::CiduClient client(settings);
        post_dify_history_response(L"difyConversations", client.list_conversations());
    }).detach();
}

// 总结：从 Dify 按当前机器 user 和会话 ID 拉取消息明细。
void run_dify_messages_async(const std::wstring& conversation_id) {
    std::thread([conversation_id]() {
        const auto settings = load_settings_with_machine_user();
        cidu_zw3d::CiduClient client(settings);
        post_dify_history_response(
            L"difyConversationMessages",
            client.list_messages(conversation_id),
            L"\"conversation_id\":\"" + cidu_zw3d::json_escape(conversation_id) + L"\"");
    }).detach();
}

// 总结：用中文顿号拼接缺失配置项，供 WebView 按钮提示用户怎么补。
std::wstring join_missing_fields(const std::vector<std::wstring>& fields) {
    std::wstring text;
    for (const auto& field : fields) {
        if (!text.empty()) {
            text += L"、";
        }
        text += field;
    }
    return text;
}

// 总结：从 WebView2 消息构造佳鱼参数化设计选型自动操作请求。
cidu_zw3d::JiavuAutomationRequest jiavu_request_from_web_message(const std::wstring& message) {
    cidu_zw3d::JiavuAutomationRequest request;
    request.action = cidu_zw3d::json_string_value(message, L"action");
    request.selected_model = cidu_zw3d::json_string_value(message, L"selected_model");
    request.template_name = cidu_zw3d::json_string_value(message, L"template_name");
    if (request.template_name.empty()) {
        request.template_name = cidu_zw3d::json_string_value(message, L"selection_template_name");
    }
    request.full_node_path = cidu_zw3d::json_string_value(message, L"selected_full_node_path");
    if (request.full_node_path.empty()) {
        request.full_node_path = cidu_zw3d::json_string_value(message, L"full_node_path");
    }
    if (request.full_node_path.empty()) {
        request.full_node_path = cidu_zw3d::json_string_value(message, L"tree_path");
    }
    if (request.full_node_path.empty()) {
        request.full_node_path = cidu_zw3d::json_string_value(message, L"node_path");
    }
    request.product_id = cidu_zw3d::json_string_value(message, L"product_id");
    if (request.product_id.empty()) {
        request.product_id = cidu_zw3d::json_string_value(message, L"productId");
    }
    request.spec_model = cidu_zw3d::json_string_value(message, L"spec_model_original");
    if (request.spec_model.empty()) {
        request.spec_model = cidu_zw3d::json_string_value(message, L"spec_model");
    }
    if (request.spec_model.empty()) {
        request.spec_model = cidu_zw3d::json_string_value(message, L"specification_model");
    }
    request.material_code_prefix = cidu_zw3d::json_string_value(message, L"material_code_prefix");
    request.parameters = cidu_zw3d::json_string_object(message, L"parameters");
    request.parameter_rows = cidu_zw3d::json_string_object_array(message, L"parameter_rows");
    request.parameter_order = cidu_zw3d::json_string_array(message, L"parameter_order");
    request.manual_parameter_overrides =
        cidu_zw3d::json_string_object(message, L"manual_parameter_overrides");
    // 只接收最近一次“开始计算”响应的原子快照；两张表不得跨计算轮次拼接。
    request.parameter_bridge_version =
        cidu_zw3d::json_string_value(message, L"parameter_bridge_version");
    request.calculation_snapshot_id =
        cidu_zw3d::json_string_value(message, L"calculation_snapshot_id");
    request.parameter_extraction_rows =
        cidu_zw3d::json_string_object_array(message, L"parameter_extraction_rows");
    request.equipment_result_rows =
        cidu_zw3d::json_string_object_array(message, L"equipment_result_rows");
    const auto parse_revision = [&](const wchar_t* key) -> std::uint64_t {
        const std::wstring value = cidu_zw3d::json_string_value(message, key);
        if (value.empty()) return 0;
        try {
            return static_cast<std::uint64_t>(std::stoull(value));
        } catch (...) {
            return 0;
        }
    };
    request.calculation_snapshot_revision = parse_revision(L"calculation_snapshot_revision");
    request.parameter_extraction_revision = parse_revision(L"parameter_extraction_revision");
    request.equipment_result_revision = parse_revision(L"equipment_result_revision");
    // 解析 manifest_params 数组
    const auto manifest_params_array = cidu_zw3d::json_string_object_array(message, L"manifest_params");
    for (const auto& item : manifest_params_array) {
        cidu_zw3d::ManifestParam param;
        auto it = item.find(L"name");
        if (it != item.end()) {
            param.name = it->second;
        }
        it = item.find(L"unit");
        if (it != item.end()) {
            param.unit = it->second;
        }
        it = item.find(L"excel_name");
        if (it != item.end()) {
            param.excel_name = it->second;
        }
        request.manifest_params.push_back(std::move(param));
    }
    const auto bridge_report = cidu_zw3d::apply_parameter_value_bridge(request);
    append_debug_log(
        "parameter_value_bridge: version=" + narrow_utf8_text(request.parameter_bridge_version) +
        " calculation_revision=" + std::to_string(request.calculation_snapshot_revision) +
        " extraction=" + std::to_string(bridge_report.extraction_count) +
        " equipment=" + std::to_string(bridge_report.equipment_count) +
        " targets=" + std::to_string(bridge_report.target_count) +
        " equipment_matched=" + std::to_string(bridge_report.matched_equipment) +
        " extraction_matched=" + std::to_string(bridge_report.matched_extraction) +
        " manual_overrides=" + std::to_string(bridge_report.manual_overrides_applied) +
        " unmatched=" + std::to_string(bridge_report.unmatched));
    return request;
}

std::wstring sanitize_excel_file_part(std::wstring value) {
    if (value.empty()) {
        value = L"jiavu_model";
    }
    for (wchar_t& ch : value) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' ||
            ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|' || std::iswcntrl(ch)) {
            ch = L'_';
        }
    }
    while (!value.empty() && (value.back() == L'.' || std::iswspace(value.back()))) {
        value.pop_back();
    }
    return value.empty() ? L"jiavu_model" : value;
}

std::wstring model_name_for_excel(const cidu_zw3d::JiavuAutomationRequest& request) {
    std::wstring value = !request.selected_model.empty() ? request.selected_model : request.template_name;
    const std::wstring suffix = L".jyasm";
    if (value.size() >= suffix.size()) {
        std::wstring tail = value.substr(value.size() - suffix.size());
        std::transform(tail.begin(), tail.end(), tail.begin(), [](wchar_t ch) {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        if (tail == suffix) {
            value.resize(value.size() - suffix.size());
        }
    }
    return sanitize_excel_file_part(value);
}

std::filesystem::path configured_model_excel_dir() {
    const cidu_zw3d::CiduSettings settings = load_settings_with_machine_user();
    if (!settings.jiavu_model_excel_dir.empty()) {
        return std::filesystem::path(settings.jiavu_model_excel_dir);
    }
    return {};
}

std::filesystem::path jiavu_excel_path_for_request(const cidu_zw3d::JiavuAutomationRequest& request) {
    const std::filesystem::path directory = configured_model_excel_dir();
    if (directory.empty()) {
        return {};
    }
    const std::wstring product_id = sanitize_excel_file_part(request.product_id);
    // 使用规格型号作为文件名的一部分，如果为空则回退到模型名称
    std::wstring spec_model = request.spec_model;
    // 去除首尾空格
    while (!spec_model.empty() && std::iswspace(spec_model.front())) {
        spec_model.erase(spec_model.begin());
    }
    while (!spec_model.empty() && std::iswspace(spec_model.back())) {
        spec_model.pop_back();
    }
    std::wstring product_name = !spec_model.empty() ? sanitize_excel_file_part(spec_model) : model_name_for_excel(request);
    if (product_id.empty() || product_id == L"jiavu_model" ||
        product_name.empty() || product_name == L"jiavu_model") {
        return {};
    }
    return directory / (product_id + L"_" + product_name + L".xlsx");
}

bool same_filesystem_path(const std::filesystem::path& left, const std::filesystem::path& right) {
    std::error_code error;
    if (std::filesystem::exists(left, error) && !error &&
        std::filesystem::exists(right, error) && !error &&
        std::filesystem::equivalent(left, right, error) && !error) {
        return true;
    }
    return left.lexically_normal() == right.lexically_normal();
}

bool has_xlsx_extension(const std::filesystem::path& path) {
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return extension == L".xlsx";
}

void cleanup_previous_excel_files(
    const std::filesystem::path& directory,
    const std::filesystem::path& keep_path) {
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error) || error) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            return;
        }
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error) || entry_error) {
            continue;
        }
        const std::filesystem::path path = entry.path();
        if (!has_xlsx_extension(path) || same_filesystem_path(path, keep_path)) {
            continue;
        }
        std::filesystem::remove(path, entry_error);
    }
}

std::wstring normalized_parameter_key(std::wstring value) {
    std::wstring normalized;
    for (wchar_t ch : value) {
        if (ch == L'[' || ch == L'【' || ch == L'(' || ch == L'（') {
            break;
        }
        if (std::iswspace(ch) || ch == L':' || ch == L'：' || ch == L'=' ||
            ch == L',' || ch == L'，' || ch == L'。' || ch == L'；' ||
            ch == L';' || ch == L'_' || ch == L'-') {
            continue;
        }
        normalized.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return normalized;
}

std::wstring trim_wide(std::wstring value) {
    while (!value.empty() && std::iswspace(value.front())) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::iswspace(value.back())) {
        value.pop_back();
    }
    return value;
}

std::wstring specification_value_for_excel(
    const cidu_zw3d::JiavuAutomationRequest& request,
    const std::map<std::wstring, std::wstring>& row) {
    if (!trim_wide(request.spec_model).empty()) {
        return trim_wide(request.spec_model);
    }
    auto it = row.find(L"规格型号");
    if (it != row.end() && !trim_wide(it->second).empty()) {
        return trim_wide(it->second);
    }
    it = request.parameters.find(L"规格型号");
    if (it != request.parameters.end() && !trim_wide(it->second).empty()) {
        return trim_wide(it->second);
    }
    return trim_wide(request.spec_model);
}

bool is_ignored_model_parameter(const std::wstring& name) {
    const std::wstring key = normalized_parameter_key(name);
    return key == L"规格型号" ||
           key == L"关键词" ||
           key == L"关键字" ||
           key == L"检索关键词" ||
           key == L"检索关键字" ||
           key == L"keyword" ||
           key == L"keywords";
}

std::vector<std::wstring> ordered_parameter_names(const cidu_zw3d::JiavuAutomationRequest& request) {
    std::vector<std::wstring> names;
    for (const auto& name : request.parameter_order) {
        if (!name.empty() && !is_ignored_model_parameter(name) &&
            request.parameters.find(name) != request.parameters.end()) {
            names.push_back(name);
        }
    }
    for (const auto& [name, _] : request.parameters) {
        if (!is_ignored_model_parameter(name) &&
            std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    }
    for (const auto& row : request.parameter_rows) {
        for (const auto& [name, _] : row) {
            if (!is_ignored_model_parameter(name) &&
                std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
            }
        }
    }
    return names;
}

std::wstring xml_escape_text(const std::wstring& value) {
    std::wstring escaped;
    for (wchar_t ch : value) {
        switch (ch) {
        case L'&':
            escaped += L"&amp;";
            break;
        case L'<':
            escaped += L"&lt;";
            break;
        case L'>':
            escaped += L"&gt;";
            break;
        case L'"':
            escaped += L"&quot;";
            break;
        case L'\'':
            escaped += L"&apos;";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::wstring excel_column_name(std::size_t column_index) {
    std::wstring name;
    std::size_t value = column_index + 1;
    while (value > 0) {
        const std::size_t remainder = (value - 1) % 26;
        name.insert(name.begin(), static_cast<wchar_t>(L'A' + remainder));
        value = (value - 1) / 26;
    }
    return name.empty() ? L"A" : name;
}

struct SharedStringTable {
    std::vector<std::wstring> values;
    std::map<std::wstring, std::size_t> indexes;

    std::size_t index_of(const std::wstring& value) {
        const auto found = indexes.find(value);
        if (found != indexes.end()) {
            return found->second;
        }
        const std::size_t index = values.size();
        values.push_back(value);
        indexes[value] = index;
        return index;
    }
};

void append_xlsx_row(
    std::wstring& xml,
    int row_number,
    const std::vector<std::wstring>& cells,
    SharedStringTable& shared_strings) {
    xml += L"    <row r=\"" + std::to_wstring(row_number) + L"\" spans=\"1:" +
           std::to_wstring(cells.size()) + L"\">";
    for (std::size_t index = 0; index < cells.size(); ++index) {
        const std::wstring ref = excel_column_name(index) + std::to_wstring(row_number);
        xml += L"<c r=\"" + ref + L"\" t=\"s\"><v>";
        xml += std::to_wstring(shared_strings.index_of(cells[index]));
        xml += L"</v></c>";
    }
    xml += L"</row>\n";
}

std::wstring build_shared_strings_xml(const SharedStringTable& shared_strings, std::size_t count) {
    std::wstring xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        L"<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" count=\"" +
        std::to_wstring(count) + L"\" uniqueCount=\"" + std::to_wstring(shared_strings.values.size()) + L"\">\n";
    for (const auto& value : shared_strings.values) {
        xml += L"  <si><t";
        if (!value.empty() && (std::iswspace(value.front()) || std::iswspace(value.back()))) {
            xml += L" xml:space=\"preserve\"";
        }
        xml += L">" + xml_escape_text(value) + L"</t></si>\n";
    }
    xml += L"</sst>\n";
    return xml;
}

const char* minimal_xlsx_styles_xml() {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"1\"><font><sz val=\"12\"/><color theme=\"1\"/><name val=\"Calibri\"/>"
        "<family val=\"2\"/><scheme val=\"minor\"/></font></fonts>"
        "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/></cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "<dxfs count=\"0\"/><tableStyles count=\"0\" defaultTableStyle=\"TableStyleMedium9\" "
        "defaultPivotStyle=\"PivotStyleMedium7\"/>"
        "</styleSheet>\n";
}

const char* minimal_xlsx_theme_xml() {
    return
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Office Theme\">"
        "<a:themeElements><a:clrScheme name=\"Office\">"
        "<a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>"
        "<a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>"
        "<a:dk2><a:srgbClr val=\"1F497D\"/></a:dk2><a:lt2><a:srgbClr val=\"EEECE1\"/></a:lt2>"
        "<a:accent1><a:srgbClr val=\"4F81BD\"/></a:accent1><a:accent2><a:srgbClr val=\"C0504D\"/></a:accent2>"
        "<a:accent3><a:srgbClr val=\"9BBB59\"/></a:accent3><a:accent4><a:srgbClr val=\"8064A2\"/></a:accent4>"
        "<a:accent5><a:srgbClr val=\"4BACC6\"/></a:accent5><a:accent6><a:srgbClr val=\"F79646\"/></a:accent6>"
        "<a:hlink><a:srgbClr val=\"0000FF\"/></a:hlink><a:folHlink><a:srgbClr val=\"800080\"/></a:folHlink>"
        "</a:clrScheme><a:fontScheme name=\"Office\"><a:majorFont><a:latin typeface=\"Cambria\"/></a:majorFont>"
        "<a:minorFont><a:latin typeface=\"Calibri\"/></a:minorFont></a:fontScheme>"
        "<a:fmtScheme name=\"Office\"><a:fillStyleLst/><a:lnStyleLst/><a:effectStyleLst/><a:bgFillStyleLst/>"
        "</a:fmtScheme></a:themeElements></a:theme>\n";
}

std::uint32_t crc32_bytes(const std::string& data) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void write_le16(std::ofstream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu),
    };
    output.write(bytes, sizeof(bytes));
}

void write_le32(std::ofstream& output, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu),
    };
    output.write(bytes, sizeof(bytes));
}

struct ZipEntry {
    std::string name;
    std::string data;
    std::string compressed_data;
    std::uint32_t crc = 0;
    std::uint32_t offset = 0;
};

std::string deflate_stored_blocks(const std::string& data) {
    std::string compressed;
    std::size_t offset = 0;
    do {
        const std::size_t remaining = data.size() - offset;
        const std::uint16_t chunk = static_cast<std::uint16_t>(
            std::min<std::size_t>(remaining, 65535));
        const bool final_block = offset + chunk >= data.size();
        compressed.push_back(static_cast<char>(final_block ? 0x01 : 0x00));
        compressed.push_back(static_cast<char>(chunk & 0xFFu));
        compressed.push_back(static_cast<char>((chunk >> 8) & 0xFFu));
        const std::uint16_t inverse = static_cast<std::uint16_t>(~chunk);
        compressed.push_back(static_cast<char>(inverse & 0xFFu));
        compressed.push_back(static_cast<char>((inverse >> 8) & 0xFFu));
        compressed.append(data.data() + offset, chunk);
        offset += chunk;
    } while (offset < data.size());
    return compressed;
}

std::pair<std::uint16_t, std::uint16_t> current_dos_zip_time() {
    std::time_t now = std::time(nullptr);
    std::tm local_time = {};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    const int raw_year = local_time.tm_year + 1900;
    const int year = raw_year < 1980 ? 1980 : raw_year;
    const std::uint16_t dos_time = static_cast<std::uint16_t>(
        (local_time.tm_hour << 11) |
        (local_time.tm_min << 5) |
        (local_time.tm_sec / 2));
    const std::uint16_t dos_date = static_cast<std::uint16_t>(
        ((year - 1980) << 9) |
        ((local_time.tm_mon + 1) << 5) |
        local_time.tm_mday);
    return {dos_time, dos_date};
}

bool write_stored_zip(const std::filesystem::path& path, std::vector<ZipEntry> entries) {
    std::ofstream output(path, std::ios::binary);
    if (!output.good()) {
        return false;
    }

    const auto [dos_time, dos_date] = current_dos_zip_time();
    for (auto& entry : entries) {
        entry.crc = crc32_bytes(entry.data);
        entry.compressed_data = deflate_stored_blocks(entry.data);
        entry.offset = static_cast<std::uint32_t>(static_cast<std::streamoff>(output.tellp()));
        write_le32(output, 0x04034B50u);
        write_le16(output, 20);
        write_le16(output, 0);
        write_le16(output, 8);
        write_le16(output, dos_time);
        write_le16(output, dos_date);
        write_le32(output, entry.crc);
        write_le32(output, static_cast<std::uint32_t>(entry.compressed_data.size()));
        write_le32(output, static_cast<std::uint32_t>(entry.data.size()));
        write_le16(output, static_cast<std::uint16_t>(entry.name.size()));
        write_le16(output, 0);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        output.write(entry.compressed_data.data(), static_cast<std::streamsize>(entry.compressed_data.size()));
    }

    const std::uint32_t central_offset =
        static_cast<std::uint32_t>(static_cast<std::streamoff>(output.tellp()));
    for (const auto& entry : entries) {
        write_le32(output, 0x02014B50u);
        write_le16(output, 20);
        write_le16(output, 20);
        write_le16(output, 0);
        write_le16(output, 8);
        write_le16(output, dos_time);
        write_le16(output, dos_date);
        write_le32(output, entry.crc);
        write_le32(output, static_cast<std::uint32_t>(entry.compressed_data.size()));
        write_le32(output, static_cast<std::uint32_t>(entry.data.size()));
        write_le16(output, static_cast<std::uint16_t>(entry.name.size()));
        write_le16(output, 0);
        write_le16(output, 0);
        write_le16(output, 0);
        write_le16(output, 0);
        write_le32(output, 0);
        write_le32(output, entry.offset);
        output.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    }
    const std::uint32_t central_size =
        static_cast<std::uint32_t>(static_cast<std::streamoff>(output.tellp())) - central_offset;

    write_le32(output, 0x06054B50u);
    write_le16(output, 0);
    write_le16(output, 0);
    write_le16(output, static_cast<std::uint16_t>(entries.size()));
    write_le16(output, static_cast<std::uint16_t>(entries.size()));
    write_le32(output, central_size);
    write_le32(output, central_offset);
    write_le16(output, 0);
    output.flush();
    return output.good();
}

bool write_xlsx_file(
    const std::filesystem::path& path,
    const std::vector<std::wstring>& names,
    const std::vector<std::vector<std::wstring>>& rows) {
    const std::wstring last_column = excel_column_name(names.empty() ? 0 : names.size() - 1);
    const int last_row = static_cast<int>(rows.size()) + 1;
    SharedStringTable shared_strings;
    const std::size_t string_count = names.size() + rows.size() * names.size();
    std::wstring sheet_xml =
        L"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        L"<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        L"xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">\n"
        L"  <dimension ref=\"A1:" + last_column + std::to_wstring(last_row) + L"\"/>\n"
        L"  <sheetViews><sheetView tabSelected=\"1\" workbookViewId=\"0\"/></sheetViews>\n"
        L"  <sheetFormatPr baseColWidth=\"10\" defaultRowHeight=\"16\"/>\n"
        L"  <sheetData>\n";
    append_xlsx_row(sheet_xml, 1, names, shared_strings);
    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index) {
        append_xlsx_row(sheet_xml, static_cast<int>(row_index) + 2, rows[row_index], shared_strings);
    }
    sheet_xml += L"  </sheetData>\n";
    sheet_xml += L"  <pageMargins left=\"0.7\" right=\"0.7\" top=\"0.75\" bottom=\"0.75\" header=\"0.3\" footer=\"0.3\"/>\n";
    sheet_xml += L"</worksheet>\n";
    const std::wstring shared_strings_xml = build_shared_strings_xml(shared_strings, string_count);

    std::vector<ZipEntry> entries = {
        {
            "[Content_Types].xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Override PartName=\"/xl/workbook.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
            "<Override PartName=\"/docProps/core.xml\" "
            "ContentType=\"application/vnd.openxmlformats-package.core-properties+xml\"/>"
            "<Override PartName=\"/docProps/app.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.extended-properties+xml\"/>"
            "<Override PartName=\"/xl/styles.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
            "<Override PartName=\"/xl/theme/theme1.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>"
            "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
            "<Override PartName=\"/xl/sharedStrings.xml\" "
            "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml\"/>"
            "</Types>\n",
        },
        {
            "_rels/.rels",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
            "Target=\"xl/workbook.xml\"/>"
            "<Relationship Id=\"rId2\" "
            "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
            "Target=\"docProps/core.xml\"/>"
            "<Relationship Id=\"rId3\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" "
            "Target=\"docProps/app.xml\"/>"
            "</Relationships>\n",
        },
        {
            "docProps/app.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" "
            "xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">"
            "<Application>CiduZW3D</Application><DocSecurity>0</DocSecurity><ScaleCrop>false</ScaleCrop>"
            "<HeadingPairs><vt:vector size=\"2\" baseType=\"variant\"><vt:variant><vt:lpstr>Worksheets</vt:lpstr>"
            "</vt:variant><vt:variant><vt:i4>1</vt:i4></vt:variant></vt:vector></HeadingPairs>"
            "<TitlesOfParts><vt:vector size=\"1\" baseType=\"lpstr\"><vt:lpstr>Sheet1</vt:lpstr></vt:vector>"
            "</TitlesOfParts><Company></Company><LinksUpToDate>false</LinksUpToDate>"
            "<SharedDoc>false</SharedDoc><HyperlinksChanged>false</HyperlinksChanged><AppVersion>16.0300</AppVersion>"
            "</Properties>\n",
        },
        {
            "docProps/core.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
            "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
            "xmlns:dcterms=\"http://purl.org/dc/terms/\" "
            "xmlns:dcmitype=\"http://purl.org/dc/dcmitype/\" "
            "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
            "<dc:creator>CiduZW3D</dc:creator><cp:lastModifiedBy>CiduZW3D</cp:lastModifiedBy>"
            "</cp:coreProperties>\n",
        },
        {
            "xl/workbook.xml",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<fileVersion appName=\"xl\" lastEdited=\"6\" lowestEdited=\"6\" rupBuild=\"26709\"/>"
            "<workbookPr/><bookViews><workbookView xWindow=\"0\" yWindow=\"460\" "
            "windowWidth=\"28800\" windowHeight=\"17460\" tabRatio=\"500\"/></bookViews>"
            "<sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
            "<calcPr calcId=\"150000\" concurrentCalc=\"0\"/>"
            "</workbook>\n",
        },
        {
            "xl/_rels/workbook.xml.rels",
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
            "Target=\"worksheets/sheet1.xml\"/>"
            "<Relationship Id=\"rId2\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" "
            "Target=\"theme/theme1.xml\"/>"
            "<Relationship Id=\"rId3\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
            "Target=\"styles.xml\"/>"
            "<Relationship Id=\"rId4\" "
            "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings\" "
            "Target=\"sharedStrings.xml\"/>"
            "</Relationships>\n",
        },
        {"xl/styles.xml", minimal_xlsx_styles_xml()},
        {"xl/theme/theme1.xml", minimal_xlsx_theme_xml()},
        {"xl/sharedStrings.xml", narrow_utf8_text(shared_strings_xml)},
        {"xl/worksheets/sheet1.xml", narrow_utf8_text(sheet_xml)},
    };
    return write_stored_zip(path, std::move(entries));
}

std::filesystem::path write_model_excel_template(
    const cidu_zw3d::JiavuAutomationRequest& request,
    int& filled_count) {
    const std::filesystem::path directory = configured_model_excel_dir();
    if (directory.empty()) {
        return {};
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return {};
    }

    const std::filesystem::path path = jiavu_excel_path_for_request(request);
    if (path.empty()) {
        return {};
    }

    // 优先使用 manifest_params 作为表头（从服务器 manifest.json 的 MAP_JY 获取）
    std::vector<std::wstring> excel_names;
    std::vector<std::wstring> names;
    if (!request.manifest_params.empty()) {
        // 完全使用 MAP_JY 定义的参数顺序和名称，不添加固定的"规格型号"列
        excel_names.reserve(request.manifest_params.size());
        for (const auto& param : request.manifest_params) {
            excel_names.push_back(param.excel_name);
            names.push_back(param.excel_name);
        }
    } else {
        // 回退到原有逻辑（保持向后兼容）
        names = ordered_parameter_names(request);
        excel_names.reserve(names.size() + 1);
        excel_names.push_back(L"规格型号");
        excel_names.insert(excel_names.end(), names.begin(), names.end());
    }

    filled_count = 0;
    std::vector<std::vector<std::wstring>> rows;
    const auto source_rows =
        request.parameter_rows.empty()
            ? std::vector<std::map<std::wstring, std::wstring>>{request.parameters}
            : request.parameter_rows;
    for (const auto& source_row : source_rows) {
        std::vector<std::wstring> row_values;
        // 如果使用 manifest_params，不固定添加规格型号
        if (request.manifest_params.empty()) {
            row_values.push_back(specification_value_for_excel(request, source_row));
        }
        bool has_value = false;
        for (const auto& name : names) {
            const auto value_it = source_row.find(name);
            const std::wstring value = value_it == source_row.end() ? L"" : value_it->second;
            if (!value.empty()) {
                has_value = true;
                ++filled_count;
            }
            row_values.push_back(value);
        }
        if (has_value || rows.empty()) {
            rows.push_back(std::move(row_values));
        }
    }

    if (!write_xlsx_file(path, excel_names, rows)) {
        return {};
    }
    cleanup_previous_excel_files(directory, path);
    return path;
}

bool wait_for_excel_ready(const std::filesystem::path& path, std::wstring& error) {
    if (path.empty()) {
        error = L"请先在设置中填写“生成模型 Excel 目录”，再生成模型。";
        return false;
    }

    std::uintmax_t last_size = static_cast<std::uintmax_t>(-1);
    int stable_ticks = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code stat_error;
        if (std::filesystem::exists(path, stat_error) && !stat_error) {
            const std::uintmax_t size = std::filesystem::file_size(path, stat_error);
            if (!stat_error && size > 0 && size == last_size) {
                ++stable_ticks;
                if (stable_ticks >= 2) {
                    return true;
                }
            } else {
                stable_ticks = 0;
            }
            last_size = stat_error ? static_cast<std::uintmax_t>(-1) : size;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    error = L"Excel 模板未在超时时间内落位或文件仍在写入，请稍后重试：";
    error += path.wstring();
    return false;
}

bool trigger_jiavu_parameter_import(std::wstring& error) {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    if (!queue_jiavu_parameter_import_command()) {
        error = L"无法把 JYParameterImport 投递到 ZW3D 主线程。";
        return false;
    }
    return true;
#else
    (void)error;
    return true;
#endif
}

// 总结：生成佳鱼要求的 "<产品id>_<产品名>.xlsx"，落位后调用 JYParameterImport。
cidu_zw3d::JiavuAutomationResult prepare_model_generation(
    const cidu_zw3d::JiavuAutomationRequest& request) {
    const std::wstring model =
        !request.selected_model.empty() ? request.selected_model : request.template_name;
    if (model.empty() && request.full_node_path.empty()) {
        return {false, L"请先确认一个模型，再生成模型。"};
    }
    if (request.product_id.empty()) {
        return {false, L"当前选型缺少产品ID，无法按佳鱼要求生成“产品id_产品名.xlsx”。"};
    }
    const std::wstring product_name = model_name_for_excel(request);
    if (product_name.empty() || product_name == L"jiavu_model") {
        return {false, L"当前选型缺少产品名称，无法按佳鱼要求生成“产品id_产品名.xlsx”。"};
    }

    int filled_count = 0;
    const std::filesystem::path excel_path = write_model_excel_template(request, filled_count);
    std::wstring wait_error;
    if (!wait_for_excel_ready(excel_path, wait_error)) {
        return {false, wait_error};
    }
    std::wstring import_error;
    if (!trigger_jiavu_parameter_import(import_error)) {
        return {false, import_error.empty() ? L"启动佳鱼参数导入命令 JYParameterImport 失败。" : import_error};
    }

    std::wostringstream message;
    message << L"已生成佳鱼参数文件并启动 JYParameterImport";
    if (!model.empty()) {
        message << L"：模型 " << model;
    }
    if (!request.template_name.empty() && request.template_name != model) {
        message << L"，模板 " << request.template_name;
    }
    const int row_count = static_cast<int>(request.parameter_rows.empty() ? 1 : request.parameter_rows.size());
    message << L"，已保存 " << row_count << L" 组参数，共 " << filled_count << L" 项。";
    message << L"文件：";
    message << excel_path.wstring();
    return {true, message.str()};
}

// 总结：执行佳鱼参数化设计选型窗口自动操作，并把结果回传给前端。
void run_jiavu_automation_async(const cidu_zw3d::JiavuAutomationRequest& request) {
    std::thread([request]() {
        queue_webview_json_message(webview_json_message(
            L"status",
            L"\"message\":\"" + cidu_zw3d::json_escape(
                request.action == L"generateModel"
                    ? L"正在准备生成模型参数..."
                    : L"正在操作佳鱼参数化设计选型窗口...") + L"\""));
        const auto result =
            request.action == L"generateModel"
                ? prepare_model_generation(request)
                : cidu_zw3d::run_jiavu_automation(request);
        queue_webview_json_message(
            L"{\"type\":\"jiavuActionResult\",\"ok\":" +
            std::wstring(result.ok ? L"true" : L"false") +
            L",\"action\":\"" + cidu_zw3d::json_escape(request.action) + L"\"" +
            L",\"message\":\"" + cidu_zw3d::json_escape(result.message) + L"\"}");
    }).detach();
}

// 总结：调用本地同步工具，把佳鱼数据库转换成插件目录下的知识库和知识图谱文件。
void run_knowledge_graph_update_async() {
    std::thread([]() {
        queue_webview_json_message(webview_json_message(
            L"knowledgeGraphUpdateStart",
            L"\"message\":\"" + cidu_zw3d::json_escape(L"正在从佳鱼数据库更新知识图谱...") + L"\""));

        const std::filesystem::path tool = knowledge_sync_tool_path();
        if (!std::filesystem::exists(tool)) {
            queue_webview_json_message(webview_json_message(
                L"knowledgeGraphUpdateResult",
                L"\"ok\":false,\"message\":\"" +
                    cidu_zw3d::json_escape(L"未找到知识图谱同步工具 JiavuKnowledgeSync.exe，请重新安装插件。") + L"\""));
            return;
        }
        std::error_code ignored;
        std::filesystem::create_directories(knowledge_output_path(), ignored);

#if defined(_WIN32)
        SECURITY_ATTRIBUTES security = {};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        HANDLE read_pipe = nullptr;
        HANDLE write_pipe = nullptr;
        if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
            queue_webview_json_message(webview_json_message(
                L"knowledgeGraphUpdateResult",
                L"\"ok\":false,\"message\":\"" + cidu_zw3d::json_escape(L"创建同步工具输出管道失败。") + L"\""));
            return;
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

        const std::wstring command =
            L"\"" + tool.wstring() + L"\" --config \"" + config_path().wstring() +
            L"\" --out \"" + knowledge_output_path().wstring() + L"\"";

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdOutput = write_pipe;
        startup.hStdError = write_pipe;

        PROCESS_INFORMATION process = {};
        std::wstring mutable_command = command;
        const BOOL created = CreateProcessW(
            nullptr,
            mutable_command.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            cidu_zw3d::module_directory().c_str(),
            &startup,
            &process);
        CloseHandle(write_pipe);
        if (!created) {
            CloseHandle(read_pipe);
            queue_webview_json_message(webview_json_message(
                L"knowledgeGraphUpdateResult",
                L"\"ok\":false,\"message\":\"" + cidu_zw3d::json_escape(L"启动知识图谱同步工具失败。") + L"\""));
            return;
        }

        std::string output;
        char buffer[4096] = {};
        DWORD read = 0;
        while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            output.append(buffer, buffer + read);
        }
        CloseHandle(read_pipe);
        WaitForSingleObject(process.hProcess, INFINITE);

        DWORD exit_code = 1;
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        std::wstring wide_output;
        if (!output.empty()) {
            const int size = MultiByteToWideChar(CP_UTF8, 0, output.data(), static_cast<int>(output.size()), nullptr, 0);
            wide_output.resize(static_cast<std::size_t>(size));
            MultiByteToWideChar(CP_UTF8, 0, output.data(), static_cast<int>(output.size()), wide_output.data(), size);
        }
        const std::wstring message = cidu_zw3d::json_string_value(wide_output, L"message");
        const bool ok = exit_code == 0 && json_bool_value(wide_output, L"ok");
        const std::wstring display =
            !message.empty()
                ? message
                : (wide_output.empty() ? L"知识图谱同步工具没有返回结果。" : wide_output);
        queue_webview_json_message(webview_json_message(
            L"knowledgeGraphUpdateResult",
            L"\"ok\":" + std::wstring(ok ? L"true" : L"false") +
                L",\"message\":\"" + cidu_zw3d::json_escape(display) + L"\""));
#else
        queue_webview_json_message(webview_json_message(
            L"knowledgeGraphUpdateResult",
            L"\"ok\":false,\"message\":\"" + cidu_zw3d::json_escape(L"知识图谱同步仅支持 Windows 安装包。") + L"\""));
#endif
    }).detach();
}

// 总结：在 WebView/Zw3D UI 线程内执行佳鱼操作，供 Qt 控件直写使用。
void run_jiavu_automation_on_ui_thread(const cidu_zw3d::JiavuAutomationRequest& request) {
    post_status_to_webview(L"正在通过佳鱼参数对象同步参数...");
    const auto result = cidu_zw3d::run_jiavu_automation(request);
    post_status_to_webview(result.message);
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (g_webview) {
        const std::wstring message =
            L"{\"type\":\"jiavuActionResult\",\"ok\":" +
            std::wstring(result.ok ? L"true" : L"false") +
            L",\"message\":\"" + cidu_zw3d::json_escape(result.message) + L"\"}";
        g_webview->PostWebMessageAsJson(message.c_str());
    }
#else
    (void)result;
#endif
}

#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
// 总结：释放嵌入表单的 WebView2 对象，必须在 ZW3D 销毁表单窗口前完成。
void shutdown_webview() {
    append_debug_log("shutdown_webview: begin");
    HWND parent = g_webview_parent;
    WNDPROC parent_proc = g_webview_parent_original_proc;
    g_webview_parent_original_proc = nullptr;
    if (parent && parent_proc && IsWindow(parent)) {
        SetWindowLongPtrW(parent, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(parent_proc));
    }
    if (g_webview && g_web_message_token.value != 0) {
        g_webview->remove_WebMessageReceived(g_web_message_token);
    }
    g_web_message_token = {};
    if (g_webview_controller) {
        g_webview_controller->Close();
    }
    g_webview.Reset();
    g_webview_controller.Reset();
    g_webview_environment.Reset();
    if (g_webview_fallback && IsWindow(g_webview_fallback)) {
        DestroyWindow(g_webview_fallback);
        g_webview_fallback = nullptr;
    }
    if (g_webview_host) {
        KillTimer(g_webview_host, kWebViewResizeTimerId);
        DestroyWindow(g_webview_host);
        g_webview_host = nullptr;
    }
    g_webview_parent = nullptr;
    g_webview_last_width = 0;
    g_webview_last_height = 0;
    append_debug_log("shutdown_webview: done");
}

// 总结：提前声明 WebView2 初始化函数，供表单初始化路径复用。
void initialize_webview(HWND parent);
// 总结：ZW3D 表单窗口子类过程，确保用户点关闭按钮时先释放 WebView2。
LRESULT CALLBACK webview_parent_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
// 总结：WebView2 host 子窗口过程，处理宿主尺寸变化。
LRESULT CALLBACK webview_host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

// 总结：给 ZW3D 浮动表单补上系统最小化/最大化按钮、可拖动边框，
// 并去掉会挤占按钮的帮助问号样式。
void enable_agent_form_minimize(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return;
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    // WS_THICKFRAME 让最大化按钮可点 + 边框可拖动调整大小。
    const LONG_PTR new_style =
        style | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    const LONG_PTR new_ex_style = ex_style & ~static_cast<LONG_PTR>(WS_EX_CONTEXTHELP);

    bool changed = false;
    if (new_style != style) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, new_style);
        changed = true;
    }
    if (new_ex_style != ex_style) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, new_ex_style);
        changed = true;
    }

    if (HMENU menu = GetSystemMenu(hwnd, FALSE)) {
        EnableMenuItem(menu, SC_MINIMIZE, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, SC_MAXIMIZE, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, SC_RESTORE, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(menu, SC_SIZE, MF_BYCOMMAND | MF_ENABLED);
    }
    if (changed) {
        SetWindowPos(
            hwnd,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        DrawMenuBar(hwnd);
        append_debug_log(
            "enable_agent_form_minimize: hwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(hwnd)) +
            " style=" + std::to_string(static_cast<long long>(new_style)) +
            " ex_style=" + std::to_string(static_cast<long long>(new_ex_style)));
    }
}

// 总结：根据当前 WebView2 父窗口大小调整浏览器控件铺满侧边栏。
void resize_webview_to_parent() {
    if (!g_webview_parent) {
        return;
    }

    RECT bounds = {};
    GetClientRect(g_webview_parent, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    const bool size_changed = width != g_webview_last_width || height != g_webview_last_height;
    g_webview_last_width = width;
    g_webview_last_height = height;
    if (g_webview_host) {
        SetWindowPos(g_webview_host, HWND_TOP, 0, 0, width, height, SWP_SHOWWINDOW);
    }
    if (g_webview_fallback && IsWindow(g_webview_fallback)) {
        RECT host_bounds = {};
        GetClientRect(g_webview_host ? g_webview_host : g_webview_parent, &host_bounds);
        const int fallback_width =
            (host_bounds.right - host_bounds.left - 32) > 100 ? (host_bounds.right - host_bounds.left - 32) : 100;
        const int fallback_height =
            (host_bounds.bottom - host_bounds.top - 32) > 80 ? (host_bounds.bottom - host_bounds.top - 32) : 80;
        SetWindowPos(
            g_webview_fallback,
            HWND_TOP,
            16,
            16,
            fallback_width,
            fallback_height,
            SWP_NOACTIVATE);
    }
    if (g_webview_controller) {
        RECT web_bounds = {};
        GetClientRect(g_webview_host ? g_webview_host : g_webview_parent, &web_bounds);
        g_webview_controller->put_Bounds(web_bounds);
        g_webview_controller->put_IsVisible(TRUE);
    }
    if (size_changed) {
        append_debug_log(
            "resize_webview_to_parent: width=" + std::to_string(width) +
            " height=" + std::to_string(height));
    }
}

// 总结：处理 ZW3D 表单窗口销毁消息，避免 WebView2 在父窗口销毁后继续访问无效句柄。
LRESULT CALLBACK webview_parent_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) {
        append_debug_log("webview_parent_proc: close -> hide");
        shutdown_webview();
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (message == WM_DESTROY || message == WM_NCDESTROY) {
        append_debug_log("webview_parent_proc: destroy");
        WNDPROC original = g_webview_parent_original_proc;
        shutdown_webview();
        if (original) {
            return CallWindowProcW(original, hwnd, message, wparam, lparam);
        }
    }
    if (g_webview_parent_original_proc) {
        return CallWindowProcW(g_webview_parent_original_proc, hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 总结：处理 WebView2 host 子窗口消息，让浏览器随子窗口尺寸变化重铺。
LRESULT CALLBACK webview_host_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case kWebViewPostJsonMessage: {
        // 后台线程只投递字符串指针，真正的 WebView2 调用仍在宿主窗口线程执行。
        std::unique_ptr<std::wstring> json(reinterpret_cast<std::wstring*>(lparam));
        if (json && g_webview) {
            g_webview->PostWebMessageAsJson(json->c_str());
        }
        return 0;
    }
    case kRunJiavuParameterImportMessage: {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
        send_jiavu_parameter_import_command();
#endif
        return 0;
    }
    case WM_SIZE:
        if (g_webview_controller) {
            RECT bounds = {};
            GetClientRect(hwnd, &bounds);
            g_webview_controller->put_Bounds(bounds);
        }
        break;
    case WM_TIMER:
        if (wparam == kWebViewResizeTimerId) {
            resize_webview_to_parent();
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 总结：从 ZW3D 表单名称获取窗口句柄，并尝试挂载 WebView2 前端。
void initialize_webview_for_form(const char* reason) {
    void* form_hwnd = nullptr;
    const auto hwnd_result = ZwUiFormHwndGet(kAgentFormName, &form_hwnd);
    append_debug_log(
        std::string("initialize_webview_for_form: reason=") + (reason ? reason : "") +
        " hwnd_result=" + std::to_string(static_cast<int>(hwnd_result)) +
        " hwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(form_hwnd)));
    if (hwnd_result == 0 && form_hwnd) {
        initialize_webview(static_cast<HWND>(form_hwnd));
    }
}

// 总结：处理 Web 前端发来的消息，并调用本机插件能力。
void handle_web_message(const std::wstring& message) {
    append_debug_log("webview_message: received");
    if (message.find(L"\"type\":\"chooseFile\"") != std::wstring::npos) {
        std::filesystem::path file_path;
        if (choose_upload_file(file_path)) {
            g_selected_files.clear();
            g_selected_files.push_back(file_path);
        }
        post_attachments_to_webview();
        return;
    }

    if (message.find(L"\"type\":\"clearFiles\"") != std::wstring::npos) {
        g_selected_files.clear();
        post_attachments_to_webview();
        return;
    }

    if (message.find(L"\"type\":\"getSettings\"") != std::wstring::npos) {
        post_settings_to_webview();
        return;
    }

    if (message.find(L"\"type\":\"listConversations\"") != std::wstring::npos) {
        run_dify_conversations_async();
        return;
    }

    if (message.find(L"\"type\":\"loadConversationMessages\"") != std::wstring::npos) {
        const std::wstring conversation_id = cidu_zw3d::json_string_value(message, L"conversation_id");
        if (conversation_id.empty()) {
            queue_webview_json_message(webview_json_message(
                L"difyConversationMessages",
                L"\"ok\":false,\"status_code\":0,\"payload\":null,\"message\":\"" +
                    cidu_zw3d::json_escape(L"缺少 Dify 会话 ID。") + L"\""));
            return;
        }
        run_dify_messages_async(conversation_id);
        return;
    }

    if (message.find(L"\"type\":\"saveSettings\"") != std::wstring::npos) {
        cidu_zw3d::CiduSettings settings = load_settings_with_machine_user();
        const std::wstring base_url = cidu_zw3d::json_string_value(message, L"base_url");
        const std::wstring api_key = cidu_zw3d::json_string_value(message, L"api_key");
        settings.base_url = base_url;
        settings.api_key = api_key;
        settings.jiavu_db_host = cidu_zw3d::json_string_value(message, L"jiavu_db_host");
        settings.jiavu_db_port = cidu_zw3d::json_string_value(message, L"jiavu_db_port");
        settings.jiavu_db_name = cidu_zw3d::json_string_value(message, L"jiavu_db_name");
        settings.jiavu_db_user = cidu_zw3d::json_string_value(message, L"jiavu_db_user");
        settings.jiavu_db_password = cidu_zw3d::json_string_value(message, L"jiavu_db_password");
        settings.jiavu_model_excel_dir = cidu_zw3d::json_string_value(message, L"jiavu_model_excel_dir");
        settings.dify_sync_base_url = cidu_zw3d::json_string_value(message, L"dify_sync_base_url");
        settings.dify_dataset_api_key = cidu_zw3d::json_string_value(message, L"dify_dataset_api_key");
        settings.dify_dataset_id = cidu_zw3d::json_string_value(message, L"dify_dataset_id");
        settings.dify_knowledge_graph_id = cidu_zw3d::json_string_value(message, L"dify_knowledge_graph_id");
        settings.dify_dataset_indexing_technique =
            cidu_zw3d::json_string_value(message, L"dify_dataset_indexing_technique");
        if (settings.dify_dataset_indexing_technique.empty()) {
            settings.dify_dataset_indexing_technique = L"economy";
        }
        settings.dify_knowledge_base_document_name =
            cidu_zw3d::json_string_value(message, L"dify_knowledge_base_document_name");
        if (settings.dify_knowledge_base_document_name.empty() ||
            settings.dify_knowledge_base_document_name == L"jiavu_knowledge_base.jsonl") {
            settings.dify_knowledge_base_document_name = L"静态配置";
        }
        settings.jyasm_service_base_url =
            cidu_zw3d::json_string_value(message, L"jyasm_service_base_url");
        settings.jyasm_service_token =
            cidu_zw3d::json_string_value(message, L"jyasm_service_token");
        settings.jyasm_service_public_base_url =
            cidu_zw3d::json_string_value(message, L"jyasm_service_public_base_url");
        const std::wstring jyasm_trigger_refresh =
            cidu_zw3d::json_string_value(message, L"jyasm_service_trigger_refresh");
        if (!jyasm_trigger_refresh.empty()) {
            settings.jyasm_service_trigger_refresh = jyasm_trigger_refresh;
        }
        const std::wstring jyasm_timeout_ms =
            cidu_zw3d::json_string_value(message, L"jyasm_service_timeout_ms");
        if (!jyasm_timeout_ms.empty()) {
            settings.jyasm_service_timeout_ms = jyasm_timeout_ms;
        }
        if (needs_machine_user_id(settings.user)) {
            settings.user = new_machine_user_id();
        }
        std::wstring save_error;
        if (!save_settings_with_fallback(settings, save_error)) {
            queue_webview_json_message(webview_json_message(
                L"settingsError",
                L"\"message\":\"" + cidu_zw3d::json_escape(L"配置文件保存失败。" + save_error) + L"\""));
            return;
        }
        if (!reload_agent_service()) {
            queue_webview_json_message(webview_json_message(
                L"settingsError",
                L"\"message\":\"" + cidu_zw3d::json_escape(L"配置已保存，但智能体服务重新加载失败。") + L"\""));
            return;
        }
        queue_webview_json_message(webview_json_message(
            L"settingsSaved",
            L"\"base_url\":\"" + cidu_zw3d::json_escape(settings.base_url) +
                L"\",\"api_key\":\"" + cidu_zw3d::json_escape(settings.api_key) +
                L"\",\"jiavu_db_host\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_host) +
                L"\",\"jiavu_db_port\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_port) +
                L"\",\"jiavu_db_name\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_name) +
                L"\",\"jiavu_db_user\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_user) +
                L"\",\"jiavu_db_password\":\"" + cidu_zw3d::json_escape(settings.jiavu_db_password) +
                L"\",\"jiavu_model_excel_dir\":\"" + cidu_zw3d::json_escape(settings.jiavu_model_excel_dir) +
                L"\",\"dify_sync_base_url\":\"" + cidu_zw3d::json_escape(settings.dify_sync_base_url) +
                L"\",\"dify_dataset_api_key\":\"" + cidu_zw3d::json_escape(settings.dify_dataset_api_key) +
                L"\",\"dify_dataset_id\":\"" + cidu_zw3d::json_escape(settings.dify_dataset_id) +
                L"\",\"dify_knowledge_graph_id\":\"" +
                    cidu_zw3d::json_escape(settings.dify_knowledge_graph_id) +
                L"\",\"dify_dataset_indexing_technique\":\"" +
                    cidu_zw3d::json_escape(settings.dify_dataset_indexing_technique) +
                L"\",\"dify_knowledge_base_document_name\":\"" +
                    cidu_zw3d::json_escape(settings.dify_knowledge_base_document_name) +
                L"\",\"jyasm_service_base_url\":\"" +
                    cidu_zw3d::json_escape(settings.jyasm_service_base_url) +
                L"\",\"jyasm_service_token\":\"" +
                    cidu_zw3d::json_escape(settings.jyasm_service_token) +
                L"\",\"jyasm_service_public_base_url\":\"" +
                    cidu_zw3d::json_escape(settings.jyasm_service_public_base_url) +
                L"\",\"jyasm_service_trigger_refresh\":\"" +
                    cidu_zw3d::json_escape(settings.jyasm_service_trigger_refresh) +
                L"\",\"jyasm_service_timeout_ms\":\"" +
                    cidu_zw3d::json_escape(settings.jyasm_service_timeout_ms) + L"\""));
        return;
    }

    if (message.find(L"\"type\":\"updateKnowledgeGraph\"") != std::wstring::npos) {
        const auto settings = load_settings_with_machine_user();
        std::vector<std::wstring> jiavu_missing;
        if (settings.jiavu_db_host.empty()) {
            jiavu_missing.push_back(L"佳鱼数据库地址");
        }
        if (settings.jiavu_db_user.empty()) {
            jiavu_missing.push_back(L"佳鱼数据库用户名");
        }
        if (!jiavu_missing.empty()) {
            const std::wstring message_text =
                L"请先在设置中补全：" + join_missing_fields(jiavu_missing) +
                L"。端口为空时默认 3306，库名为空时会自动识别。";
            post_status_to_webview(message_text);
            queue_webview_json_message(webview_json_message(
                L"knowledgeGraphUpdateResult",
                L"\"ok\":false,\"message\":\"" + cidu_zw3d::json_escape(message_text) + L"\""));
            return;
        }
        const bool has_dify_field =
            !settings.dify_sync_base_url.empty() ||
            !settings.dify_dataset_api_key.empty() ||
            !settings.dify_dataset_id.empty() ||
            !settings.dify_knowledge_graph_id.empty();
        const bool has_sync_target =
            !settings.dify_dataset_id.empty() || !settings.dify_knowledge_graph_id.empty();
        const bool has_sync_url =
            !settings.dify_sync_base_url.empty() || !settings.base_url.empty();
        if (has_dify_field &&
            (!has_sync_url || settings.dify_dataset_api_key.empty() || !has_sync_target)) {
            std::vector<std::wstring> dify_missing;
            if (!has_sync_url) {
                dify_missing.push_back(L"Dify 同步地址（例如 http://192.168.10.32/v1）");
            }
            if (settings.dify_dataset_api_key.empty()) {
                dify_missing.push_back(L"Dataset API Key");
            }
            if (!has_sync_target) {
                dify_missing.push_back(L"知识库 ID 或知识图谱 ID 至少一个");
            }
            const std::wstring message_text =
                L"Dify 同步配置不完整，请在设置中补全：" + join_missing_fields(dify_missing) +
                L"。只想生成本地文件时，请清空所有 Dify 同步配置。";
            queue_webview_json_message(webview_json_message(
                L"knowledgeGraphUpdateResult",
                L"\"ok\":false,\"message\":\"" + cidu_zw3d::json_escape(message_text) + L"\""));
            return;
        }
        run_knowledge_graph_update_async();
        return;
    }

    if (message.find(L"\"type\":\"ask\"") != std::wstring::npos) {
        const std::wstring prompt = cidu_zw3d::json_string_value(message, L"prompt");
        const std::wstring conversation_id = cidu_zw3d::json_string_value(message, L"conversation_id");
        const bool automatic_recalculation = json_bool_value(message, L"automatic_recalculation");
        if (prompt.empty()) {
            post_status_to_webview(L"请先输入选型需求。");
            return;
        }

        post_status_to_webview(
            automatic_recalculation
                ? L"正在分析参数依赖并自动重新计算..."
                : L"正在同步参数并执行设备选型计算...");
        // 自动重算只提交本轮参数修改，不得误上传或清空用户尚未发送的附件。
        const std::vector<std::filesystem::path> request_files =
            automatic_recalculation ? std::vector<std::filesystem::path>{} : g_selected_files;
        run_agent_request_streaming_async(prompt, request_files, conversation_id);
        if (!automatic_recalculation) {
            // 附件已随本轮请求拷贝进流式线程；清空本地队列避免下一轮重复上传。
            g_selected_files.clear();
            post_attachments_to_webview();
        }
        return;
    }

    if (message.find(L"\"type\":\"jiavuAction\"") != std::wstring::npos) {
        const auto request = jiavu_request_from_web_message(message);
        if (request.action.empty()) {
            post_status_to_webview(L"缺少佳鱼操作类型。");
            return;
        }
        if (request.action == L"insert" || request.action == L"open") {
            post_status_to_webview(L"请在佳鱼参数化设计页面点击“插入”或“打开”。");
            return;
        }
        if (request.action == L"generateModel") {
            run_jiavu_automation_async(request);
        } else {
            run_jiavu_automation_async(request);
        }
        return;
    }
}

// 总结：初始化 WebView2 控件，并把本地 HTML 前端加载到 ZW3D 表单窗口内。
void initialize_webview(HWND parent) {
    if (!parent) {
        append_debug_log("initialize_webview: empty parent hwnd");
        return;
    }
    if (g_webview_controller && g_webview_parent == parent) {
        enable_agent_form_minimize(parent);
        resize_webview_to_parent();
        return;
    }

    g_webview_parent = parent;
    enable_agent_form_minimize(parent);
    if (!g_webview_parent_original_proc) {
        g_webview_parent_original_proc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(parent, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(webview_parent_proc)));
        append_debug_log(
            "initialize_webview: subclassed parent=" + std::to_string(reinterpret_cast<std::uintptr_t>(parent)));
    }
    if (!g_webview_host || GetParent(g_webview_host) != parent) {
        static bool host_class_registered = false;
        if (!host_class_registered) {
            WNDCLASSW window_class = {};
            window_class.lpfnWndProc = webview_host_proc;
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpszClassName = L"CiduAgentWebViewHost";
            window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassW(&window_class);
            host_class_registered = true;
        }

        g_webview_host = CreateWindowExW(
            0,
            L"CiduAgentWebViewHost",
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0,
            0,
            1,
            1,
            parent,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        SetTimer(g_webview_host, kWebViewResizeTimerId, 200, nullptr);
        append_debug_log(
            "initialize_webview: host=" + std::to_string(reinterpret_cast<std::uintptr_t>(g_webview_host)));
        if (!g_webview_host) {
            show_webview_fallback(L"Cidu Agent 窗口创建失败：无法创建 WebView 宿主窗口。\r\n请查看 apilibs\\cidu_agent_debug.log。");
            append_debug_log("initialize_webview: CreateWindowExW host failed");
            return;
        }
    }
    resize_webview_to_parent();

    const std::filesystem::path user_data = cidu_zw3d::module_directory() / L"webview2_user_data";
    const HRESULT env_result = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        user_data.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [parent](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || !environment) {
                    append_debug_log(
                        "initialize_webview: environment creation failed result=" + hresult_hex(result));
                    show_webview_fallback(
                        L"Cidu Agent 页面加载失败：WebView2 环境创建失败。\r\n"
                        L"请确认已安装 Microsoft Edge WebView2 Runtime。\r\n"
                        L"错误码：" + hresult_hex_wide(result) +
                        L"\r\n日志：apilibs\\cidu_agent_debug.log");
                    return S_OK;
                }

                g_webview_environment = environment;
                environment->CreateCoreWebView2Controller(
                    g_webview_host ? g_webview_host : parent,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [](HRESULT controller_result, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controller_result) || !controller) {
                                append_debug_log(
                                    "initialize_webview: controller creation failed result=" +
                                    hresult_hex(controller_result));
                                show_webview_fallback(
                                    L"Cidu Agent 页面加载失败：WebView2 控件创建失败。\r\n"
                                    L"请重启 ZW3D 后再试，或查看 apilibs\\cidu_agent_debug.log。\r\n"
                                    L"错误码：" + hresult_hex_wide(controller_result));
                                return S_OK;
                            }

                            g_webview_controller = controller;
                            g_webview_controller->get_CoreWebView2(&g_webview);
                            resize_webview_to_parent();
                            if (g_webview) {
                                hide_webview_fallback();
                                g_webview->add_WebMessageReceived(
                                    Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args)
                                            -> HRESULT {
                                            (void)sender;
                                            LPWSTR raw_message = nullptr;
                                            if (SUCCEEDED(args->TryGetWebMessageAsString(&raw_message)) && raw_message) {
                                                handle_web_message(raw_message);
                                                CoTaskMemFree(raw_message);
                                            }
                                            return S_OK;
                                        })
                                        .Get(),
                                    &g_web_message_token);

                                const std::wstring html = compose_web_panel_html(read_text_file(web_panel_path()));
                                if (html.empty()) {
                                    append_debug_log("initialize_webview: agent_panel.html is empty or missing");
                                    g_webview->NavigateToString(
                                        L"<html><body style='font-family:Segoe UI'>cidu-agent web panel missing.</body></html>");
                                } else {
                                    const HRESULT navigate_result = g_webview->NavigateToString(html.c_str());
                                    if (FAILED(navigate_result)) {
                                        append_debug_log(
                                            "initialize_webview: NavigateToString failed result=" +
                                            hresult_hex(navigate_result));
                                        show_webview_fallback(
                                            L"Cidu Agent 页面加载失败：WebView2 导航失败。\r\n"
                                            L"错误码：" + hresult_hex_wide(navigate_result) +
                                            L"\r\n日志：apilibs\\cidu_agent_debug.log");
                                    }
                                }
                            } else {
                                append_debug_log("initialize_webview: controller returned null CoreWebView2");
                                show_webview_fallback(
                                    L"Cidu Agent 页面加载失败：WebView2 页面对象为空。\r\n"
                                    L"请查看 apilibs\\cidu_agent_debug.log。");
                            }
                            append_debug_log("initialize_webview: ready");
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(env_result)) {
        append_debug_log(
            "initialize_webview: CreateCoreWebView2EnvironmentWithOptions failed result=" +
            hresult_hex(env_result));
        show_webview_fallback(
            L"Cidu Agent 页面加载失败：无法启动 WebView2。\r\n"
            L"请确认已安装 Microsoft Edge WebView2 Runtime。\r\n"
            L"错误码：" + hresult_hex_wide(env_result) +
            L"\r\n日志：apilibs\\cidu_agent_debug.log");
    }
}
#endif

// 总结：打开文件选择窗口，让用户选择要上传给 Cidu 智能体的附件。
bool choose_upload_file(std::filesystem::path& path) {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    char selected_path[4096] = {};
    const std::string prompt = narrow_text(L"选择要上传给 cidu-agent 的附件");
    const std::string filter = narrow_text(
        L"文档|*.txt;*.md;*.mdx;*.markdown;*.pdf;*.html;*.htm;*.xlsx;*.xls;"
        L"*.doc;*.docx;*.csv;*.eml;*.msg;*.pptx;*.ppt;*.xml;*.epub|所有文件|*.*|");
    // 在 ZW3D 表单回调里优先使用宿主原生文件对话框，避免系统对话框被主窗口遮挡。
    cvxGetFileByLongPath(
        1,
        prompt.c_str(),
        nullptr,
        filter.c_str(),
        selected_path,
        static_cast<int>(sizeof(selected_path)));
    if (!selected_path[0]) {
        append_debug_log("choose_upload_file: native dialog returned empty path");
        return false;
    }

    path = widen_prompt(selected_path);
    append_debug_log("choose_upload_file: native dialog selected path");
    return true;
#elif defined(_WIN32)
    wchar_t file_name[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter =
        L"文档\0*.txt;*.md;*.mdx;*.markdown;*.pdf;*.html;*.htm;*.xlsx;*.xls;"
        L"*.doc;*.docx;*.csv;*.eml;*.msg;*.pptx;*.ppt;*.xml;*.epub\0"
        L"所有文件\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) != TRUE) {
        return false;
    }

    path = file_name;
    return true;
#else
    (void)path;
    return false;
#endif
}

#if defined(CIDU_ZW3D_WITH_REAL_SDK)
// 总结：把宽字符串按本机编码写入指定表单控件，保证中文提示在 ZW3D 里正常显示。
void set_form_text(int field_id, const std::wstring& text) {
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    (void)field_id;
    (void)text;
#else
    std::string display = narrow_text(text);
    cvxItemSet(
        const_cast<char*>(kAgentFormName),
        field_id,
        1,
        display.empty() ? const_cast<char*>("") : display.data());
#endif
}

// 总结：刷新附件状态控件，让用户知道本次提交会上传哪个文件。
void refresh_attachment_field() {
    set_form_text(kAttachmentFieldId, selected_file_summary());
}
#endif

// 总结：处理命令行形式的 Agent 调用，并把结果显示到 ZW3D 消息区。
int CiduZW3DAskAgentCommand(char* prompt) {
    const wchar_t* result = CiduZW3DAskAndGenerate(widen_prompt(prompt).c_str());
    g_host.show_message(result ? result : L"{\"ok\":false,\"message\":\"No result.\"}");
    return 0;
}

// 总结：标记 Agent 表单已准备显示；浮动表单不挂入 ZW3D 原生 Dock，避免占用其他插件侧边栏。
int ensure_agent_sidebar_registered() {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    if (g_sidebar_registered) {
        return 0;
    }

    append_debug_log(
        "ensure_agent_sidebar_registered: floating state=" + std::to_string(cvxFormState(kAgentFormName)));
    g_sidebar_registered = true;
#endif
    return 0;
}

// 总结：提供手动命令入口，便于在 ZW3D 命令行里重新触发右侧栏注册并查看日志。
int register_agent_sidebar_command() {
    return ensure_agent_sidebar_registered();
}

// 总结：调用佳鱼提供的 ZW3D 参数导入命令。
void send_jiavu_parameter_import_command() {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    append_debug_log("JYParameterImport: calling cvxCmdSend(\"~JYParameterImport\")");
    cvxCmdSend("~JYParameterImport");
    append_debug_log("JYParameterImport: cvxCmdSend returned");
#endif
}

// 总结：把用户输入和插件响应整理成聊天窗口里的显示文本。
std::wstring chat_text(const std::wstring& prompt, const std::wstring& raw_result) {
    std::wstring message = cidu_zw3d::json_string_value(raw_result, L"message");
    if (message.empty()) {
        message = raw_result.empty() ? L"No result." : raw_result;
    }
    const std::wstring answer_markdown = cidu_zw3d::json_string_value(raw_result, L"answer_markdown");
    if (!answer_markdown.empty()) {
        std::wstring text = L"你\n";
        text += prompt;
        text += L"\n\nCidu\n";
        text += answer_markdown;
        return text;
    }

    const std::wstring created_file = cidu_zw3d::json_string_value(raw_result, L"created_file");
    const std::wstring selected_model = cidu_zw3d::json_string_value(raw_result, L"selected_model");
    const std::wstring model_description = cidu_zw3d::json_string_value(raw_result, L"model_description");
    const std::wstring template_name = cidu_zw3d::json_string_value(raw_result, L"template_name");
    const auto requirements = cidu_zw3d::json_string_object(raw_result, L"parameter_requirements");
    const auto numeric = cidu_zw3d::json_number_object(raw_result, L"numeric");
    const auto text_parameters = cidu_zw3d::json_string_object(raw_result, L"text");

    // 聊天窗口只显示用户关心的信息，底层完整 JSON 仍保留给命令和调试使用。
    std::wstring text = L"你\n";
    text += prompt;
    text += L"\n\nCidu\n";

    text += L"选型模型\n";
    if (selected_model.empty()) {
        text += L"- 暂无选型模型，请检查 Cidu 工作流是否返回 selected_model。\n";
    } else {
        text += L"- 名称：";
        text += selected_model;
        text += L"\n";
    }
    if (!model_description.empty()) {
        text += L"- 说明：";
        text += model_description;
        text += L"\n";
    }
    if (!template_name.empty() && template_name != selected_model) {
        text += L"- 参数化模板：";
        text += template_name;
        text += L"\n";
    }

    text += L"\n模型需要的参数\n";
    text += format_string_object(requirements, L"- Cidu 未返回参数需求说明，请在工作流中补充 parameter_requirements。");
    text += L"\n\n已识别的数值参数\n";
    text += format_number_object(numeric, L"- 暂无数值参数");
    text += L"\n\n已识别的文本参数\n";
    text += format_string_object(text_parameters, L"- 暂无文本参数");

    text += L"\n\n选型结果\n";
    text += message;
    if (!created_file.empty()) {
        text += L"\n\nCreated file: ";
        text += created_file;
    }
    return text;
}

// 总结：读取表单输入，调用 Cidu 选型链路，并把结果写回表单。
int submit_agent_prompt() {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    char prompt_buffer[4096] = {};
    cvxItemGet(const_cast<char*>(kAgentFormName), kPromptFieldId, 1, prompt_buffer);

    const std::wstring prompt = widen_prompt(prompt_buffer);
    if (prompt.empty()) {
        // 空输入不触发网络请求，直接提示用户补充建模描述。
        set_form_text(kResultFieldId, L"Cidu\n请先描述你要选择和创建的模型。");
        cvxFieldFocusSet(const_cast<char*>(kAgentFormName), kPromptFieldId);
        return 0;
    }

    // 请求可能耗时，先给用户一个即时状态反馈。
    std::wstring waiting_text = L"Cidu\n正在进行 jiavu 混合检索选型...";
    if (!g_selected_files.empty()) {
        waiting_text += L"\n正在上传附件：";
        waiting_text += selected_file_summary();
    }
    set_form_text(kResultFieldId, waiting_text);

    const auto result = g_service->ask_and_generate(prompt, g_selected_files);
    g_last_result = format_agent_result_json(result);

    const std::wstring prompt_with_files = prompt + selected_file_chat_note();
    const std::wstring display_wide = chat_text(prompt_with_files, g_last_result);
    set_form_text(kResultFieldId, display_wide);
    return 0;
#else
    g_host.show_message(L"Cidu ZW3D form is only available inside ZW3D.");
    return 0;
#endif
}

// 总结：显示 Agent 聊天表单，不存在时先创建表单实例。
int show_agent_form() {
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (!g_com_initialized) {
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        g_com_initialized = SUCCEEDED(com_result);
        if (com_result == RPC_E_CHANGED_MODE) {
            append_debug_log("show_agent_form: COM already initialized with another model");
        }
    }
#endif
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
#endif
    ensure_agent_sidebar_registered();

    const int state_before = cvxFormState(kAgentFormName);
    if (state_before == 0) {
        const int create_result = cvxFormCreate(kAgentFormName, 0);
        append_debug_log(
            "show_agent_form: state_before=" + std::to_string(state_before) +
            " create_result=" + std::to_string(create_result));
        if (create_result != 0) {
            g_host.show_message(L"Failed to create Cidu ZW3D form. Check cidu_agent_debug.log.");
            return 1;
        }
    }
    cvxFormShow(const_cast<char*>(kAgentFormName));
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    void* form_hwnd = nullptr;
    const auto hwnd_result = ZwUiFormHwndGet(kAgentFormName, &form_hwnd);
    if (hwnd_result == 0 && form_hwnd) {
        HWND hwnd = static_cast<HWND>(form_hwnd);
        enable_agent_form_minimize(hwnd);
        ShowWindow(hwnd, SW_RESTORE);
        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 80, 80, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        append_debug_log(
            "show_agent_form: restore hwnd=" + std::to_string(reinterpret_cast<std::uintptr_t>(hwnd)));
    }
    const szwPixel agent_position{80, 80};
    const auto position_result = ZwUiFormPositionSet(
        kAgentFormName,
        "screen",
        agent_position,
        ZW_FORM_MOVE_FROM_TOP_LEFT);
    append_debug_log("show_agent_form: position_result=" + std::to_string(static_cast<int>(position_result)));
#endif
    append_debug_log("show_agent_form: state_after=" + std::to_string(cvxFormState(kAgentFormName)));
    if (cvxFormState(kAgentFormName) == 0) {
        g_host.show_message(L"Failed to create Cidu ZW3D form.");
        return 1;
    }
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    initialize_webview_for_form("show_agent_form");
#endif
    return 0;
#else
    g_host.show_message(L"Cidu ZW3D form is only available inside ZW3D.");
    return 0;
#endif
}

// 总结：处理 ZW3D 表单生命周期事件和标准按钮事件。
int CiduZW3DAgentFormCommand(int action, int* unused) {
    (void)unused;
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (action == VX_FORM_CANCEL) {
        shutdown_webview();
        return 0;
    }
#endif

    if (action == VX_FORM_INIT) {
        // 表单首次创建时清空输入，并展示空状态提示。
        g_selected_files.clear();
#if !defined(CIDU_ZW3D_WITH_WEBVIEW2)
        set_form_text(kPromptFieldId, L"");
        refresh_attachment_field();
        set_form_text(
            kResultFieldId,
            L"Cidu\n描述你要选择的模型或系统，我会展示选型模型、检索依据和参数需求。");
        cvxFieldFocusSet(const_cast<char*>(kAgentFormName), kPromptFieldId);
#endif
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
        initialize_webview_for_form("form_init");
#endif
        return 0;
    }

#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    if (g_webview_parent && IsWindow(g_webview_parent)) {
        resize_webview_to_parent();
    }
#endif

    if (action == VX_FORM_APPLY || action == VX_FORM_OKAY) {
        return submit_agent_prompt();
    }
#else
    (void)action;
#endif
    return 0;
}

// 总结：处理聊天表单里的自定义按钮点击事件。
int CiduZW3DAgentFormCallback(char* form, int id_field, int item) {
    (void)form;
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    append_debug_log(
        "form_callback: field=" + std::to_string(id_field) +
        " item=" + std::to_string(item));
    if (id_field == kActionButtonFieldId && item == 1) {
        return submit_agent_prompt();
    }
    if (id_field == kActionButtonFieldId && item == 2) {
        g_selected_files.clear();
        set_form_text(kPromptFieldId, L"");
        refresh_attachment_field();
        set_form_text(
            kResultFieldId,
            L"Cidu\n描述你要选择的模型或系统，我会展示选型模型、检索依据和参数需求。");
        cvxFieldFocusSet(const_cast<char*>(kAgentFormName), kPromptFieldId);
    }
    if (id_field == kActionButtonFieldId && item == 3) {
        std::filesystem::path file_path;
        if (choose_upload_file(file_path)) {
            g_selected_files.clear();
            g_selected_files.push_back(file_path);
            refresh_attachment_field();
            append_debug_log("form_callback: attachment selected");
        } else {
            set_form_text(kAttachmentFieldId, L"未选择附件");
            append_debug_log("form_callback: attachment selection canceled or failed");
        }
        return 0;
    }
    if (id_field == kActionButtonFieldId && item == 4) {
        g_selected_files.clear();
        refresh_attachment_field();
        return 0;
    }
#else
    (void)id_field;
    (void)item;
#endif
    return 0;
}

}  // namespace

// 总结：初始化 Agent 插件服务，注册命令回调和表单回调。
extern "C" CIDU_ZW3D_API int CiduZW3DAgentInitialize() {
    append_debug_log("CiduZW3DAgentInitialize: build=2026-07-16-repeat-model-edit-v6");
    append_debug_log(
        "CiduZW3DAgentInitialize: module_directory=" +
        narrow_utf8_text(cidu_zw3d::module_directory().wstring()));
    if (!reload_agent_service()) {
        return 1;
    }
    // 注册命令入口：一个用于手动命令调用，一个用于 Ribbon 按钮打开表单。
    const bool ask_registered = g_host.register_command_handler(
        L"CiduZW3DAskAgent",
        reinterpret_cast<void*>(CiduZW3DAskAgentCommand));
    const bool form_registered = g_host.register_command_handler(
        L"CiduZW3DShowAgentForm",
        reinterpret_cast<void*>(show_agent_form));
    const bool sidebar_registered = g_host.register_command_handler(
        L"CiduZW3DRegisterSidebar",
        reinterpret_cast<void*>(register_agent_sidebar_command));
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    // 注册表单执行函数和控件回调，供 zrc 里的 UI 资源按名称调用。
    cvxFormFunc(
        const_cast<char*>(kAgentFormName),
        reinterpret_cast<void*>(CiduZW3DAgentFormCommand),
        VX_CODE_GENERAL);
    cvxCmdCallback(
        const_cast<char*>("CiduZW3DAgentFormCb"),
        reinterpret_cast<void*>(CiduZW3DAgentFormCallback));
    // 启动时自动打开 Agent 窗口，同时保留 Ribbon 入口用于关闭后重新打开。
    cvxCmdBuffer("~CiduZW3DShowAgentForm", 0);
#endif
    return ask_registered && form_registered && sidebar_registered ? 0 : 1;
}

// 总结：卸载 Agent 插件注册的命令和表单回调，并释放服务对象。
extern "C" CIDU_ZW3D_API int CiduZW3DAgentShutdown() {
    g_host.unregister_command(L"CiduZW3DAskAgent");
    g_host.unregister_command(L"CiduZW3DShowAgentForm");
    g_host.unregister_command(L"CiduZW3DRegisterSidebar");
#if defined(CIDU_ZW3D_WITH_REAL_SDK)
    cvxCmdFuncUnload(kAgentFormName);
    cvxCmdFuncUnload("CiduZW3DAgentFormCb");
#endif
#if defined(CIDU_ZW3D_WITH_WEBVIEW2)
    shutdown_webview();
    if (g_com_initialized) {
        CoUninitialize();
        g_com_initialized = false;
    }
#endif
    g_service.reset();
    return 0;
}

// 总结：ZW3D 按 DLL 名称约定调用的 Agent 插件初始化入口。
extern "C" CIDU_ZW3D_API int ZW3DAgentPluginInit() {
    return CiduZW3DAgentInitialize();
}

// 总结：ZW3D 按 DLL 名称约定调用的 Agent 插件退出入口。
extern "C" CIDU_ZW3D_API int ZW3DAgentPluginExit() {
    return CiduZW3DAgentShutdown();
}

// 总结：导出给命令、表单和外部调用方使用的“询问并生成模型”接口。
extern "C" CIDU_ZW3D_API const wchar_t* CiduZW3DAskAndGenerate(const wchar_t* prompt) {
    if (!g_service) {
        CiduZW3DAgentInitialize();
    }
    if (!prompt || !*prompt) {
        g_last_result = L"{\"ok\":false,\"message\":\"Prompt is empty.\"}";
        return g_last_result.c_str();
    }

    // 返回值使用静态宽字符串缓存，保证跨 DLL 调用后指针仍然有效。
    const auto result = g_service->ask_and_generate(prompt);
    g_last_result = format_agent_result_json(result);
    return g_last_result.c_str();
}

#if defined(_WIN32)
// 总结：Windows DLL 入口；当前不做额外初始化，只保持可加载。
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)module;
    (void)reason;
    (void)reserved;
    return TRUE;
}
#endif
