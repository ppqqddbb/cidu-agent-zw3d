#pragma once

#include "cidu_client.h"

#include <filesystem>

namespace cidu_zw3d {

// 总结：从配置文件读取 Cidu 服务端连接参数。
CiduSettings load_cidu_settings(const std::filesystem::path& file);

// 总结：把 Cidu 服务端连接参数保存到配置文件。
bool save_cidu_settings(const std::filesystem::path& file, const CiduSettings& settings);

// 总结：把 Cidu 服务端连接参数保存到配置文件，失败时返回具体原因。
bool save_cidu_settings(
    const std::filesystem::path& file,
    const CiduSettings& settings,
    std::wstring* error_message);

// 总结：获取当前 DLL 或可执行文件所在目录，用于定位旁路配置和插件 DLL。
std::filesystem::path module_directory();

}  // namespace cidu_zw3d
