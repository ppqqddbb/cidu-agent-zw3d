#include "cidu_zw3d/model_parameters.h"

#include "cidu_zw3d/json_protocol.h"

#include <sstream>

namespace cidu_zw3d {

// 总结：把模型参数对象写成参数化插件可消费的 JSON 文本。
std::wstring to_json(const ModelParameters& parameters) {
    std::wostringstream out;
    out << L"{\"template_name\":\"" << json_escape(parameters.template_name) << L"\",\"numeric\":{";
    bool first = true;
    for (const auto& [key, value] : parameters.numeric) {
        if (!first) {
            out << L",";
        }
        out << L"\"" << json_escape(key) << L"\":" << value;
        first = false;
    }
    out << L"},\"text\":{";
    first = true;
    // text 对象保留非数值参数，避免材料、备注等信息被塞进 numeric。
    for (const auto& [key, value] : parameters.text) {
        if (!first) {
            out << L",";
        }
        out << L"\"" << json_escape(key) << L"\":\"" << json_escape(value) << L"\"";
        first = false;
    }
    out << L"}}";
    return out.str();
}

// 总结：从 JSON 文本中恢复模型参数对象，允许只返回部分参数字段。
bool from_json(const std::wstring& json, ModelParameters& parameters) {
    // 这里仅解析参数化插件真正需要的字段，选型展示字段由 AgentService 单独解析。
    parameters.template_name = json_string_value(json, L"template_name");
    parameters.numeric = json_number_object(json, L"numeric");
    parameters.text = json_string_object(json, L"text");
    return !parameters.template_name.empty() || !parameters.numeric.empty() || !parameters.text.empty();
}

}  // namespace cidu_zw3d
