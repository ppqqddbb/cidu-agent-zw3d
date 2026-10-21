#pragma once

#include <map>
#include <string>

namespace cidu_zw3d {

// 总结：描述参数化建模插件需要消费的模型参数集合。
struct ModelParameters {
    // 参数化插件内部识别的模板名称，例如 flange、shaft、bracket。
    std::wstring template_name;
    // 数值型参数集合，适合尺寸、数量、角度等可计算参数。
    std::map<std::wstring, double> numeric;
    // 文本型参数集合，适合材料、名称、工艺备注等非数值参数。
    std::map<std::wstring, std::wstring> text;
};

// 总结：描述一次模型生成调用的执行结果。
struct ModelGenerationResult {
    // 建模流程是否成功完成。
    bool ok = false;
    // 给用户或上层插件显示的执行说明。
    std::wstring message;
    // 如果建模流程生成或保存了文件，这里记录文件路径。
    std::wstring created_file;
};

// 总结：把模型参数序列化为 Agent 插件和参数化插件之间传递的 JSON。
std::wstring to_json(const ModelParameters& parameters);

// 总结：从 Cidu 或命令行返回的 JSON 中解析模型参数。
bool from_json(const std::wstring& json, ModelParameters& parameters);

}  // namespace cidu_zw3d
