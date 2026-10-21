#include "cidu_zw3d/plugin_config.h"

#include "cidu_zw3d/json_protocol.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cidu_zw3d {

namespace {

std::string narrow_utf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string output(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, output.data(), size, nullptr, nullptr);
    return output;
#else
    return std::string(value.begin(), value.end());
#endif
}

std::wstring widen_utf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }
#if defined(_WIN32)
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size);
    return output;
#else
    return std::wstring(value.begin(), value.end());
#endif
}

std::wstring format_error(const std::filesystem::path& file, const std::wstring& reason) {
    return L"路径：" + file.wstring() + L"；原因：" + reason;
}

std::wstring system_error_text(const std::error_code& error) {
    if (!error) {
        return L"未知错误";
    }
#if defined(_WIN32)
    const std::string message = error.message();
    if (!message.empty()) {
        const int size = MultiByteToWideChar(CP_ACP, 0, message.c_str(), -1, nullptr, 0);
        if (size > 1) {
            std::wstring output(static_cast<std::size_t>(size - 1), L'\0');
            MultiByteToWideChar(CP_ACP, 0, message.c_str(), -1, output.data(), size);
            return output;
        }
    }
#endif
    return L"错误码 " + std::to_wstring(error.value());
}

std::wstring last_win32_error_text() {
#if defined(_WIN32)
    const DWORD code = GetLastError();
    if (code == ERROR_SUCCESS) {
        return L"未知错误";
    }
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    std::wstring message = size > 0 && buffer ? std::wstring(buffer, size) : L"错误码 " + std::to_wstring(code);
    if (buffer) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'.')) {
        message.pop_back();
    }
    return message;
#else
    return L"打开文件失败";
#endif
}

std::wstring settings_json(const CiduSettings& settings) {
    std::wostringstream output;
    output << L"{\n";
    output << L"  \"base_url\": \"" << json_escape(settings.base_url) << L"\",\n";
    output << L"  \"api_key\": \"" << json_escape(settings.api_key) << L"\",\n";
    output << L"  \"user\": \"" << json_escape(settings.user) << L"\",\n";
    output << L"  \"jiavu_db_host\": \"" << json_escape(settings.jiavu_db_host) << L"\",\n";
    output << L"  \"jiavu_db_port\": \"" << json_escape(settings.jiavu_db_port) << L"\",\n";
    output << L"  \"jiavu_db_name\": \"" << json_escape(settings.jiavu_db_name) << L"\",\n";
    output << L"  \"jiavu_db_user\": \"" << json_escape(settings.jiavu_db_user) << L"\",\n";
    output << L"  \"jiavu_db_password\": \"" << json_escape(settings.jiavu_db_password) << L"\",\n";
    output << L"  \"dify_sync_base_url\": \"" << json_escape(settings.dify_sync_base_url) << L"\",\n";
    output << L"  \"dify_dataset_api_key\": \"" << json_escape(settings.dify_dataset_api_key) << L"\",\n";
    output << L"  \"dify_dataset_id\": \"" << json_escape(settings.dify_dataset_id) << L"\",\n";
    output << L"  \"dify_knowledge_graph_id\": \"" << json_escape(settings.dify_knowledge_graph_id) << L"\",\n";
    output << L"  \"dify_dataset_indexing_technique\": \""
           << json_escape(settings.dify_dataset_indexing_technique) << L"\",\n";
    output << L"  \"dify_knowledge_base_document_name\": \""
           << json_escape(settings.dify_knowledge_base_document_name) << L"\",\n";
    output << L"  \"jiavu_model_excel_dir\": \"" << json_escape(settings.jiavu_model_excel_dir) << L"\",\n";
    output << L"  \"jyasm_service_base_url\": \"" << json_escape(settings.jyasm_service_base_url) << L"\",\n";
    output << L"  \"jyasm_service_token\": \"" << json_escape(settings.jyasm_service_token) << L"\",\n";
    output << L"  \"jyasm_service_public_base_url\": \""
           << json_escape(settings.jyasm_service_public_base_url) << L"\",\n";
    output << L"  \"jyasm_service_trigger_refresh\": \""
           << json_escape(settings.jyasm_service_trigger_refresh) << L"\",\n";
    output << L"  \"jyasm_service_timeout_ms\": \""
           << json_escape(settings.jyasm_service_timeout_ms) << L"\"\n";
    output << L"}\n";
    return output.str();
}

}  // namespace

// 总结：获取当前模块目录，方便插件在同目录查找配置和配套 DLL。
std::filesystem::path module_directory() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH]{};
    HMODULE module = nullptr;
    // 通过函数地址反查当前 DLL/EXE 句柄，避免依赖进程当前工作目录。
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&module_directory),
        &module);
    GetModuleFileNameW(module, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

// 总结：读取 Cidu 配置文件，缺失字段时保留默认配置。
CiduSettings load_cidu_settings(const std::filesystem::path& file) {
    CiduSettings settings;
    // 用字节流读取后再按 UTF-8 转 wstring；之前用 wifstream 会走 locale ACP，
    // 把保存时的 UTF-8 字节当成本地代码页解码，导致 "静态配置" 等中文回填后变乱码。
    std::ifstream input(file, std::ios::binary);
    if (!input.good()) {
        // 找不到配置文件时使用空连接配置，避免安装包默认连错环境。
        return settings;
    }
    std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    std::wstring content = widen_utf8(raw);
    // 配置文件格式很小，只提取当前插件实际需要的字符串字段。
    const auto base_url = json_string_value(content, L"base_url");
    const auto api_key = json_string_value(content, L"api_key");
    const auto user = json_string_value(content, L"user");
    const auto jiavu_db_host = json_string_value(content, L"jiavu_db_host");
    const auto jiavu_db_port = json_string_value(content, L"jiavu_db_port");
    const auto jiavu_db_name = json_string_value(content, L"jiavu_db_name");
    const auto jiavu_db_user = json_string_value(content, L"jiavu_db_user");
    const auto jiavu_db_password = json_string_value(content, L"jiavu_db_password");
    const auto dify_sync_base_url = json_string_value(content, L"dify_sync_base_url");
    const auto dify_dataset_api_key = json_string_value(content, L"dify_dataset_api_key");
    const auto dify_dataset_id = json_string_value(content, L"dify_dataset_id");
    const auto dify_knowledge_graph_id = json_string_value(content, L"dify_knowledge_graph_id");
    const auto dify_dataset_indexing_technique =
        json_string_value(content, L"dify_dataset_indexing_technique");
    const auto dify_knowledge_base_document_name =
        json_string_value(content, L"dify_knowledge_base_document_name");
    const auto jiavu_model_excel_dir = json_string_value(content, L"jiavu_model_excel_dir");
    const auto jyasm_service_base_url = json_string_value(content, L"jyasm_service_base_url");
    const auto jyasm_service_token = json_string_value(content, L"jyasm_service_token");
    const auto jyasm_service_public_base_url =
        json_string_value(content, L"jyasm_service_public_base_url");
    const auto jyasm_service_trigger_refresh =
        json_string_value(content, L"jyasm_service_trigger_refresh");
    const auto jyasm_service_timeout_ms = json_string_value(content, L"jyasm_service_timeout_ms");

    if (!base_url.empty()) {
        settings.base_url = base_url;
    }
    if (!api_key.empty()) {
        settings.api_key = api_key;
    }
    if (!user.empty() && user != L"zw3d-user") {
        settings.user = user;
    }
    settings.jiavu_db_host = jiavu_db_host;
    settings.jiavu_db_port = jiavu_db_port;
    settings.jiavu_db_name = jiavu_db_name;
    settings.jiavu_db_user = jiavu_db_user;
    settings.jiavu_db_password = jiavu_db_password;
    settings.dify_sync_base_url = dify_sync_base_url;
    settings.dify_dataset_api_key = dify_dataset_api_key;
    settings.dify_dataset_id = dify_dataset_id;
    settings.dify_knowledge_graph_id = dify_knowledge_graph_id;
    if (!dify_dataset_indexing_technique.empty()) {
        settings.dify_dataset_indexing_technique = dify_dataset_indexing_technique;
    }
    if (!dify_knowledge_base_document_name.empty()) {
        settings.dify_knowledge_base_document_name = dify_knowledge_base_document_name;
    }
    settings.jiavu_model_excel_dir = jiavu_model_excel_dir;
    settings.jyasm_service_base_url = jyasm_service_base_url;
    settings.jyasm_service_token = jyasm_service_token;
    settings.jyasm_service_public_base_url = jyasm_service_public_base_url;
    if (!jyasm_service_trigger_refresh.empty()) {
        settings.jyasm_service_trigger_refresh = jyasm_service_trigger_refresh;
    }
    if (!jyasm_service_timeout_ms.empty()) {
        settings.jyasm_service_timeout_ms = jyasm_service_timeout_ms;
    }
    return settings;
}

// 总结：写回 Cidu 配置文件，供插件窗口设置面板持久化连接参数。
bool save_cidu_settings(const std::filesystem::path& file, const CiduSettings& settings) {
    return save_cidu_settings(file, settings, nullptr);
}

bool save_cidu_settings(
    const std::filesystem::path& file,
    const CiduSettings& settings,
    std::wstring* error_message) {
    std::error_code error;
    const std::filesystem::path parent = file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            if (error_message) {
                *error_message = format_error(file, L"创建目录失败：" + system_error_text(error));
            }
            return false;
        }
    }

    const std::string content = narrow_utf8(settings_json(settings));
#if defined(_WIN32)
    HANDLE handle = CreateFileW(
        file.wstring().c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error_message) {
            *error_message = format_error(file, L"打开文件失败：" + last_win32_error_text());
        }
        return false;
    }

    DWORD written = 0;
    const BOOL ok = WriteFile(
        handle,
        content.data(),
        static_cast<DWORD>(content.size()),
        &written,
        nullptr);
    const DWORD write_error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!ok || written != content.size()) {
        if (error_message) {
            if (write_error != ERROR_SUCCESS) {
                SetLastError(write_error);
            }
            *error_message = format_error(file, L"写入文件失败：" + last_win32_error_text());
        }
        return false;
    }
    return true;
#else
    std::ofstream output(file, std::ios::binary);
    if (!output.good()) {
        if (error_message) {
            *error_message = format_error(file, L"打开文件失败");
        }
        return false;
    }
    output << content;
    if (!output.good()) {
        if (error_message) {
            *error_message = format_error(file, L"写入文件失败");
        }
        return false;
    }
    return true;
#endif
}

}  // namespace cidu_zw3d
