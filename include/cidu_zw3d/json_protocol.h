#pragma once

#include <map>
#include <string>
#include <vector>

namespace cidu_zw3d {

// 总结：转义宽字符串，使其可以安全写入 JSON 字符串字段。
std::wstring json_escape(const std::wstring& value);

// 总结：从简单 JSON 文本中读取指定键的字符串值。
std::wstring json_string_value(const std::wstring& json, const std::wstring& key);

// 总结：从简单 JSON 文本中读取指定键下的数字对象。
std::map<std::wstring, double> json_number_object(const std::wstring& json, const std::wstring& key);

// 总结：从简单 JSON 文本中读取指定键下的字符串对象。
std::map<std::wstring, std::wstring> json_string_object(const std::wstring& json, const std::wstring& key);

// 总结：从简单 JSON 文本中读取指定键下的字符串对象数组。
std::vector<std::map<std::wstring, std::wstring>> json_string_object_array(
    const std::wstring& json,
    const std::wstring& key);

// 总结：从简单 JSON 文本中读取指定键下的字符串数组。
std::vector<std::wstring> json_string_array(const std::wstring& json, const std::wstring& key);

}  // namespace cidu_zw3d
