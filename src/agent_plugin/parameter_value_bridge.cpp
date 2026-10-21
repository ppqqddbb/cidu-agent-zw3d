#include "parameter_value_bridge.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace cidu_zw3d {

namespace {

struct ValueRecord {
    std::wstring name;
    std::wstring value;
    std::wstring unit;
    std::wstring full_key;
    std::wstring bare_key;
    std::size_t sequence = 0;
};

std::wstring trim_copy(const std::wstring& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

wchar_t normalize_full_width(wchar_t ch) {
    if (ch >= L'！' && ch <= L'～') {
        return static_cast<wchar_t>(ch - 0xFEE0);
    }
    if (ch == L'　') return L' ';
    if (ch == L'【') return L'[';
    if (ch == L'】') return L']';
    if (ch == L'（') return L'(';
    if (ch == L'）') return L')';
    return ch;
}

void replace_all(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::wstring::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

std::wstring canonical_key(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (wchar_t raw : trim_copy(value)) {
        const wchar_t ch = normalize_full_width(raw);
        if (std::iswspace(ch) || ch == L'`' || ch == L'*') {
            continue;
        }
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    replace_all(result, L"梳形", L"梳型");
    return result;
}

std::wstring normalized_header(const std::wstring& value) {
    std::wstring key = canonical_key(value);
    key.erase(
        std::remove_if(key.begin(), key.end(), [](wchar_t ch) {
            return ch == L'/' || ch == L':' || ch == L'_' || ch == L'-';
        }),
        key.end());
    return key;
}

std::wstring first_row_value(
    const std::map<std::wstring, std::wstring>& row,
    const std::vector<std::wstring>& names) {
    for (const auto& name : names) {
        const auto direct = row.find(name);
        if (direct != row.end() && !trim_copy(direct->second).empty()) {
            return direct->second;
        }
    }
    std::vector<std::wstring> accepted;
    accepted.reserve(names.size());
    for (const auto& name : names) accepted.push_back(normalized_header(name));
    for (const auto& [key, value] : row) {
        if (trim_copy(value).empty()) continue;
        const std::wstring normalized = normalized_header(key);
        if (std::find(accepted.begin(), accepted.end(), normalized) != accepted.end()) {
            return value;
        }
    }
    return L"";
}

bool valid_value(const std::wstring& value) {
    const std::wstring text = canonical_key(value);
    static const std::set<std::wstring> invalid = {
        L"", L"-", L"null", L"none", L"undefined", L"nan", L"n/a", L"na",
        L"未识别", L"待补充", L"待确认", L"未知", L"未找到明确依据", L"缺失"};
    return invalid.find(text) == invalid.end();
}

std::wstring normalize_unit(std::wstring unit) {
    unit = trim_copy(unit);
    std::wstring result;
    for (wchar_t raw : unit) {
        const wchar_t ch = normalize_full_width(raw);
        if (std::iswspace(ch) || ch == L'[' || ch == L']' || ch == L'(' || ch == L')') {
            continue;
        }
        result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    if (result == L"毫米") return L"mm";
    if (result == L"厘米") return L"cm";
    if (result == L"米") return L"m";
    if (result == L"度" || result == L"°") return L"deg";
    if (result == L"立方米" || result == L"m³" || result == L"m^3") return L"m3";
    return result;
}

std::wstring unit_from_name(const std::wstring& name) {
    const std::size_t left = name.find_last_of(L"[【（(");
    if (left == std::wstring::npos) return L"";
    const std::size_t right = name.find_first_of(L"]】）)", left + 1);
    if (right == std::wstring::npos || right <= left + 1) return L"";
    return normalize_unit(name.substr(left + 1, right - left - 1));
}

bool name_has_unit(const std::wstring& name) {
    return !unit_from_name(name).empty();
}

std::wstring bare_name(const std::wstring& name) {
    const std::size_t left = name.find_last_of(L"[【（(");
    if (left == std::wstring::npos) return trim_copy(name);
    const std::size_t right = name.find_first_of(L"]】）)", left + 1);
    if (right == std::wstring::npos) return trim_copy(name);
    if (!trim_copy(name.substr(right + 1)).empty()) return trim_copy(name);
    return trim_copy(name.substr(0, left));
}

std::wstring display_name(const std::wstring& name, const std::wstring& unit) {
    const std::wstring clean_name = trim_copy(name);
    const std::wstring clean_unit = trim_copy(unit);
    if (clean_name.empty() || clean_unit.empty() || name_has_unit(clean_name)) {
        return clean_name;
    }
    return clean_name + L" [" + clean_unit + L"]";
}

std::wstring alias_name(const std::wstring& name) {
    static const std::map<std::wstring, std::wstring> aliases = {
        {canonical_key(L"锅炉下联箱标高"), L"下联箱标高 [mm]"},
        {canonical_key(L"锅炉下联箱标高 T1"), L"下联箱标高 [mm]"},
        {canonical_key(L"下联箱梳形板长度"), L"锅炉梳型密封板长度 [mm]"},
        {canonical_key(L"下联箱梳形板宽度"), L"锅炉梳型密封板宽度 [mm]"},
        {canonical_key(L"关断门箱体长度"), L"锅炉梳型密封板长度 [mm]"},
        {canonical_key(L"关断门箱体宽度"), L"锅炉梳型密封板宽度 [mm]"},
        {canonical_key(L"关断门箱体高度"), L"L密封板自身高度 [mm]"},
        {canonical_key(L"关断门口间距"), L"渣井口间距 [mm]"},
        {canonical_key(L"渣井下口标高"), L"渣井壁板下口高度 [mm]"},
        {canonical_key(L"渣井壁板上口标高"), L"渣井壁板上口高度 [mm]"},
        {canonical_key(L"渣井左右壁板角度"), L"渣井左右端壁板角度 [度]"},

        // 新增 (2024-07-21): 机械密封相关参数
        {canonical_key(L"L密封板自身高度"), L"L密封板自身高度 [mm]"},
        {canonical_key(L"L密封板与密封梁角钢水平距离"), L"L密封板与密封梁角钢水平距离 [mm]"},
        {canonical_key(L"L密封板与密封梁角钢高度差"), L"L密封板与密封梁角钢高度差 [mm]"},
        {canonical_key(L"密封形式"), L"密封形式"},
        {canonical_key(L"锅炉梳型密封板连接螺栓高度差"), L"锅炉梳型密封板连接螺栓高度差 [mm]"},

        // 新增 (2024-07-21): 渣井壳体组件相关参数
        {canonical_key(L"渣井壁板上口宽度"), L"渣井壁板上口宽度 [mm]"},
        {canonical_key(L"渣井壁板下口长度"), L"渣井壁板下口长度 [mm]"},
        {canonical_key(L"渣井中间壁板顶端高度"), L"渣井中间壁板顶端高度 [mm]"},
        {canonical_key(L"渣井中间壁板顶端间距"), L"渣井中间壁板顶端间距 [mm]"},
        {canonical_key(L"渣井中间端壁板角度"), L"渣井中间端壁板角度 [度]"},

        // 新增 (2024-07-21): 鳞斗干渣机相关参数
        {canonical_key(L"鳞斗干渣机弧段定位距离"), L"鳞斗干渣机弧段定位距离 [mm]"},
        {canonical_key(L"鳞斗干渣机弧段定位尺寸"), L"鳞斗干渣机弧段定位距离 [mm]"},
        {canonical_key(L"弧段定位尺寸"), L"鳞斗干渣机弧段定位距离 [mm]"},
        {canonical_key(L"弧段侧的关断门箱体边缘距离"), L"弧段侧的关断门箱体边缘距离 [mm]"},
        {canonical_key(L"鳞斗干渣机干渣机倾角"), L"鳞斗干渣机干渣机倾角 [度]"},

        // 新增 (2024-07-21): 渣井斜梯相关参数
        {canonical_key(L"渣井斜梯类型"), L"渣井斜梯类型"},
        {canonical_key(L"渣井斜梯定位类型"), L"渣井斜梯定位类型"},
        {canonical_key(L"关断门平台类型"), L"关断门平台类型"},

        // 新增 (2024-07-21): 其他参数
        {canonical_key(L"锅炉下膨胀量"), L"锅炉下膨胀量 [mm]"},
        {canonical_key(L"渣井口数"), L"渣井口数"},
        {canonical_key(L"渣井偏心距离"), L"渣井偏心距离 [mm]"},
    };
    const auto found = aliases.find(canonical_key(name));
    return found == aliases.end() ? trim_copy(name) : found->second;
}

std::wstring unit_from_value(const std::wstring& value) {
    const std::wstring text = trim_copy(value);
    wchar_t* end = nullptr;
    std::wcstod(text.c_str(), &end);
    if (!end || end == text.c_str()) return L"";
    return normalize_unit(text.substr(static_cast<std::size_t>(end - text.c_str())));
}

std::vector<ValueRecord> records_from_rows(
    const std::vector<std::map<std::wstring, std::wstring>>& rows,
    bool equipment) {
    std::vector<ValueRecord> records;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        const std::wstring name = first_row_value(
            row,
            equipment
                ? std::vector<std::wstring>{L"参数", L"参数名称", L"设备名称"}
                : std::vector<std::wstring>{L"参数中文名称", L"参数名称", L"中文名称", L"参数名"});
        const std::wstring value = first_row_value(
            row,
            equipment
                ? std::vector<std::wstring>{L"参数值", L"设备型号/参数", L"型号/参数", L"使用值", L"数值", L"值"}
                : std::vector<std::wstring>{L"数值", L"参数值", L"提取数值", L"识别结果", L"使用值", L"当前值", L"值"});
        if (trim_copy(name).empty() || !valid_value(value)) continue;
        std::wstring unit = first_row_value(row, {L"单位"});
        if (trim_copy(unit).empty()) unit = unit_from_name(name);
        if (trim_copy(unit).empty()) unit = unit_from_value(value);
        const std::wstring resolved_name = alias_name(display_name(name, unit));
        records.push_back({
            resolved_name,
            trim_copy(value),
            normalize_unit(unit),
            canonical_key(resolved_name),
            canonical_key(bare_name(resolved_name)),
            index});
    }
    return records;
}

const ValueRecord* find_record(
    const std::vector<ValueRecord>& records,
    const std::wstring& full_key,
    const std::wstring& bare_key,
    const std::wstring& target_unit) {
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        if (it->full_key == full_key) return &*it;
    }
    std::vector<const ValueRecord*> candidates;
    for (const auto& record : records) {
        if (record.bare_key != bare_key) continue;
        if (!target_unit.empty() && !record.unit.empty() && record.unit != target_unit) continue;
        candidates.push_back(&record);
    }
    if (candidates.empty()) return nullptr;
    const std::wstring first_value = trim_copy(candidates.front()->value);
    const bool conflict = std::any_of(candidates.begin(), candidates.end(), [&](const ValueRecord* record) {
        return trim_copy(record->value) != first_value;
    });
    return conflict ? nullptr : candidates.back();
}

std::wstring format_number(double value) {
    if (std::fabs(value) < 1e-12) value = 0.0;
    std::wostringstream stream;
    stream << std::setprecision(15) << std::defaultfloat << value;
    return stream.str();
}

std::wstring value_for_target(const ValueRecord& record, const std::wstring& target_unit_raw) {
    const std::wstring text = trim_copy(record.value);
    wchar_t* end = nullptr;
    const double numeric = std::wcstod(text.c_str(), &end);
    if (!end || end == text.c_str()) return text;
    const std::wstring number_text = trim_copy(text.substr(0, static_cast<std::size_t>(end - text.c_str())));
    std::wstring source_unit = record.unit;
    if (source_unit.empty()) source_unit = normalize_unit(text.substr(static_cast<std::size_t>(end - text.c_str())));
    const std::wstring target_unit = normalize_unit(target_unit_raw);
    if (source_unit.empty() || target_unit.empty()) return number_text;
    if (source_unit == target_unit) return number_text;
    const std::map<std::wstring, double> length_to_meter = {
        {L"mm", 0.001}, {L"cm", 0.01}, {L"m", 1.0}};
    const auto source = length_to_meter.find(source_unit);
    const auto target = length_to_meter.find(target_unit);
    if (source != length_to_meter.end() && target != length_to_meter.end()) {
        return format_number(numeric * source->second / target->second);
    }
    return number_text;
}

}  // namespace

ParameterValueBridgeReport apply_parameter_value_bridge(JiavuAutomationRequest& request) {
    ParameterValueBridgeReport report;
    const std::vector<ValueRecord> extraction = records_from_rows(request.parameter_extraction_rows, false);
    const std::vector<ValueRecord> equipment = records_from_rows(request.equipment_result_rows, true);
    report.extraction_count = extraction.size();
    report.equipment_count = equipment.size();

    const bool strict_calculation_snapshot = request.parameter_bridge_version == L"v3";
    const bool valid_atomic_revision = request.calculation_snapshot_revision > 0 &&
        request.parameter_extraction_revision == request.calculation_snapshot_revision &&
        request.equipment_result_revision == request.calculation_snapshot_revision;
    const auto apply_manual_overrides = [&]() {
        for (const auto& [name, value] : request.manual_parameter_overrides) {
            if (name.empty()) continue;
            request.parameters[name] = value;
            if (std::find(request.parameter_order.begin(), request.parameter_order.end(), name) ==
                request.parameter_order.end()) {
                request.parameter_order.push_back(name);
            }
            for (auto& row : request.parameter_rows) row[name] = value;
            ++report.manual_overrides_applied;
        }
    };
    if (strict_calculation_snapshot && !valid_atomic_revision) {
        report.target_count = request.manifest_params.size();
        report.unmatched = report.target_count;
        apply_manual_overrides();
        return report;
    }

    for (const auto& param : request.manifest_params) {
        const std::wstring model_name = !param.name.empty() ? param.name : param.excel_name;
        const std::wstring storage_name = !param.excel_name.empty() ? param.excel_name : model_name;
        if (model_name.empty() || storage_name.empty()) continue;
        ++report.target_count;
        const std::wstring target_name = alias_name(display_name(model_name, param.unit));
        const std::wstring full_key = canonical_key(target_name);
        const std::wstring bare_key = canonical_key(bare_name(target_name));
        const std::wstring target_unit = normalize_unit(!param.unit.empty() ? param.unit : unit_from_name(target_name));

        const ValueRecord* equipment_record = find_record(equipment, full_key, bare_key, target_unit);
        const ValueRecord* extraction_record = find_record(extraction, full_key, bare_key, target_unit);
        const ValueRecord* record = nullptr;
        bool from_equipment = false;
        if (strict_calculation_snapshot) {
            // v3 的两张表属于同一条“开始计算”响应；同名时设备选型结果是最终计算值。
            record = equipment_record ? equipment_record : extraction_record;
            from_equipment = equipment_record != nullptr;
        } else if (equipment_record) {
            const bool has_revisions = request.parameter_extraction_revision > 0 ||
                                       request.equipment_result_revision > 0;
            const bool equipment_is_newer = !has_revisions ||
                request.equipment_result_revision >= request.parameter_extraction_revision;
            if (extraction_record && !equipment_is_newer) {
                record = extraction_record;
            } else {
                record = equipment_record;
                from_equipment = true;
            }
        } else {
            record = extraction_record;
        }
        if (!record) {
            ++report.unmatched;
            continue;
        }
        const std::wstring value = value_for_target(*record, target_unit);
        if (!valid_value(value)) {
            ++report.unmatched;
            continue;
        }
        request.parameters[storage_name] = value;
        if (std::find(request.parameter_order.begin(), request.parameter_order.end(), storage_name) ==
            request.parameter_order.end()) {
            request.parameter_order.push_back(storage_name);
        }
        for (auto& row : request.parameter_rows) row[storage_name] = value;
        if (from_equipment) {
            ++report.matched_equipment;
        } else {
            ++report.matched_extraction;
        }
    }
    // 最新计算结果负责模型的默认权威回填；用户随后在模型参数表中的显式修改拥有最高优先级。
    apply_manual_overrides();
    return report;
}

}  // namespace cidu_zw3d
