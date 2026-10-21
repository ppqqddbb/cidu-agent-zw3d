#include "cidu_zw3d/json_protocol.h"

#include <cwctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace cidu_zw3d {

namespace {

// 总结：把单个十六进制字符转换成数值，用于解析 JSON 的 \uXXXX 转义。
int hex_value(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') {
        return static_cast<int>(ch - L'0');
    }
    if (ch >= L'a' && ch <= L'f') {
        return static_cast<int>(ch - L'a' + 10);
    }
    if (ch >= L'A' && ch <= L'F') {
        return static_cast<int>(ch - L'A' + 10);
    }
    return -1;
}

// 总结：读取 JSON 的四位十六进制 Unicode 码点。
bool read_hex4(const std::wstring& json, std::size_t pos, wchar_t& value) {
    if (pos + 4 > json.size()) {
        return false;
    }

    int code = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const int digit = hex_value(json[pos + index]);
        if (digit < 0) {
            return false;
        }
        code = (code << 4) | digit;
    }
    value = static_cast<wchar_t>(code);
    return true;
}

// 总结：定位指定 JSON 键对应值的起始位置。
std::size_t find_value_start(const std::wstring& json, const std::wstring& key) {
    const std::wstring quoted_key = L"\"" + key + L"\"";
    const auto key_pos = json.find(quoted_key);
    if (key_pos == std::wstring::npos) {
        return std::wstring::npos;
    }
    const auto colon_pos = json.find(L':', key_pos + quoted_key.size());
    if (colon_pos == std::wstring::npos) {
        return std::wstring::npos;
    }
    auto value_pos = colon_pos + 1;
    while (value_pos < json.size() && std::iswspace(json[value_pos])) {
        ++value_pos;
    }
    return value_pos;
}

// 总结：读取 JSON 字符串字面量，并处理常见转义字符。
std::wstring read_quoted(const std::wstring& json, std::size_t quote_pos, std::size_t* next_pos = nullptr) {
    if (quote_pos == std::wstring::npos || quote_pos >= json.size() || json[quote_pos] != L'"') {
        return L"";
    }

    std::wstring value;
    for (std::size_t pos = quote_pos + 1; pos < json.size(); ++pos) {
        const wchar_t ch = json[pos];
        if (ch == L'\\' && pos + 1 < json.size()) {
            const wchar_t escaped = json[++pos];
            switch (escaped) {
            case L'"':
            case L'\\':
            case L'/':
                value.push_back(escaped);
                break;
            case L'n':
                value.push_back(L'\n');
                break;
            case L'r':
                value.push_back(L'\r');
                break;
            case L't':
                value.push_back(L'\t');
                break;
            case L'u': {
                wchar_t code = 0;
                if (read_hex4(json, pos + 1, code)) {
                    value.push_back(code);
                    pos += 4;
                } else {
                    value.push_back(escaped);
                }
                break;
            }
            default:
                value.push_back(escaped);
                break;
            }
            continue;
        }
        if (ch == L'"') {
            if (next_pos) {
                *next_pos = pos + 1;
            }
            return value;
        }
        value.push_back(ch);
    }
    return value;
}

// 总结：读取指定键下对象值的内部文本，用于后续解析键值对。
std::wstring object_body(const std::wstring& json, const std::wstring& key) {
    const auto value_pos = find_value_start(json, key);
    if (value_pos == std::wstring::npos || value_pos >= json.size() || json[value_pos] != L'{') {
        return L"";
    }

    int depth = 0;
    bool in_string = false;
    // 只在字符串外统计花括号深度，避免 JSON 字符串里的 `{}` 干扰对象边界。
    for (std::size_t pos = value_pos; pos < json.size(); ++pos) {
        const wchar_t ch = json[pos];
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
        } else if (ch == L'}') {
            --depth;
            if (depth == 0) {
                return json.substr(value_pos + 1, pos - value_pos - 1);
            }
        }
    }
    return L"";
}

// 总结：读取指定键下数组值的内部文本，用于解析字符串数组。
std::wstring array_body(const std::wstring& json, const std::wstring& key) {
    const auto value_pos = find_value_start(json, key);
    if (value_pos == std::wstring::npos || value_pos >= json.size() || json[value_pos] != L'[') {
        return L"";
    }

    int depth = 0;
    bool in_string = false;
    for (std::size_t pos = value_pos; pos < json.size(); ++pos) {
        const wchar_t ch = json[pos];
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
        if (ch == L'[') {
            ++depth;
        } else if (ch == L']') {
            --depth;
            if (depth == 0) {
                return json.substr(value_pos + 1, pos - value_pos - 1);
            }
        }
    }
    return L"";
}

}  // namespace

// 总结：转义 JSON 字符串中的引号、反斜杠和控制字符。
std::wstring json_escape(const std::wstring& value) {
    std::wstring escaped;
    for (const wchar_t ch : value) {
        switch (ch) {
        case L'"':
            escaped += L"\\\"";
            break;
        case L'\\':
            escaped += L"\\\\";
            break;
        case L'\n':
            escaped += L"\\n";
            break;
        case L'\r':
            escaped += L"\\r";
            break;
        case L'\t':
            escaped += L"\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

// 总结：从简单 JSON 中提取指定键的字符串值。
std::wstring json_string_value(const std::wstring& json, const std::wstring& key) {
    const auto value_pos = find_value_start(json, key);
    if (value_pos == std::wstring::npos || value_pos >= json.size() || json[value_pos] != L'"') {
        return L"";
    }
    return read_quoted(json, value_pos);
}

// 总结：从简单 JSON 中提取指定对象里的数字键值对。
std::map<std::wstring, double> json_number_object(const std::wstring& json, const std::wstring& key) {
    std::map<std::wstring, double> values;
    const std::wstring body = object_body(json, key);
    std::size_t pos = 0;
    // 当前协议只需要解析一层对象，避免为插件引入额外 JSON 依赖。
    while ((pos = body.find(L'"', pos)) != std::wstring::npos) {
        std::size_t next = 0;
        const std::wstring item_key = read_quoted(body, pos, &next);
        const auto colon = body.find(L':', next);
        if (colon == std::wstring::npos) {
            break;
        }
        // 数字字段直接截取到逗号或对象结尾，再交给标准库转换。
        const auto end = body.find_first_of(L",}", colon + 1);
        const std::wstring number = body.substr(colon + 1, end - colon - 1);
        values[item_key] = std::wcstod(number.c_str(), nullptr);
        pos = end == std::wstring::npos ? body.size() : end + 1;
    }
    return values;
}

// 总结：从简单 JSON 中提取指定对象里的字符串键值对。
std::map<std::wstring, std::wstring> json_string_object(const std::wstring& json, const std::wstring& key) {
    std::map<std::wstring, std::wstring> values;
    const std::wstring body = object_body(json, key);
    std::size_t pos = 0;
    // 字符串对象用于 text 参数和 parameter_requirements 参数说明。
    while ((pos = body.find(L'"', pos)) != std::wstring::npos) {
        std::size_t next = 0;
        const std::wstring item_key = read_quoted(body, pos, &next);
        const auto colon = body.find(L':', next);
        const auto quote = body.find(L'"', colon);
        if (colon == std::wstring::npos || quote == std::wstring::npos) {
            break;
        }
        values[item_key] = read_quoted(body, quote, &next);
        pos = next;
    }
    return values;
}

// 总结：从简单 JSON 中提取指定数组里的字符串对象值。
std::vector<std::map<std::wstring, std::wstring>> json_string_object_array(
    const std::wstring& json,
    const std::wstring& key) {
    std::vector<std::map<std::wstring, std::wstring>> values;
    const std::wstring body = array_body(json, key);
    std::size_t pos = 0;
    while ((pos = body.find(L'{', pos)) != std::wstring::npos) {
        int depth = 0;
        bool in_string = false;
        for (std::size_t cursor = pos; cursor < body.size(); ++cursor) {
            const wchar_t ch = body[cursor];
            if (ch == L'\\' && in_string) {
                ++cursor;
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
            } else if (ch == L'}') {
                --depth;
                if (depth == 0) {
                    const std::wstring object = body.substr(pos, cursor - pos + 1);
                    values.push_back(json_string_object(L"{\"row\":" + object + L"}", L"row"));
                    pos = cursor + 1;
                    break;
                }
            }
        }
    }
    return values;
}

// 总结：从简单 JSON 中提取指定数组里的字符串值。
std::vector<std::wstring> json_string_array(const std::wstring& json, const std::wstring& key) {
    std::vector<std::wstring> values;
    const std::wstring body = array_body(json, key);
    std::size_t pos = 0;
    while ((pos = body.find(L'"', pos)) != std::wstring::npos) {
        std::size_t next = 0;
        std::wstring value = read_quoted(body, pos, &next);
        if (!value.empty()) {
            values.push_back(std::move(value));
        }
        pos = next;
    }
    return values;
}

}  // namespace cidu_zw3d
