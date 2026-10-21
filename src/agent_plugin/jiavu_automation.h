#pragma once

#include <map>
#include <cstdint>
#include <string>
#include <vector>

namespace cidu_zw3d {

struct ManifestParam {
    std::wstring name;
    std::wstring unit;
    std::wstring excel_name;
};

struct JiavuAutomationRequest {
    std::wstring action;
    std::wstring selected_model;
    std::wstring template_name;
    std::wstring full_node_path;
    // 产品ID（MySQL product.id 串）。佳鱼导入接口按 “<产品id>_<产品名>.xlsx” 查找 Excel。
    std::wstring product_id;
    // 佳鱼 Excel 模板首列”规格型号”。优先由前端从 manifest 原始字段传入。
    std::wstring spec_model;
    std::wstring material_code_prefix;
    std::map<std::wstring, std::wstring> parameters;
    std::vector<std::map<std::wstring, std::wstring>> parameter_rows;
    std::vector<std::wstring> parameter_order;
    // 用户在“设计模型/参数说明”表中明确手动修改的值。参数桥先应用最新计算快照，
    // 再应用此映射，保证第二次及后续生成模型时手动修改不会被旧计算值覆盖。
    std::map<std::wstring, std::wstring> manual_parameter_overrides;
    // 最近一次“开始计算”响应的原子快照。输入核对与设备结果必须属于同一轮计算。
    std::wstring parameter_bridge_version;
    std::wstring calculation_snapshot_id;
    std::uint64_t calculation_snapshot_revision = 0;
    std::vector<std::map<std::wstring, std::wstring>> parameter_extraction_rows;
    std::vector<std::map<std::wstring, std::wstring>> equipment_result_rows;
    // 兼容旧版 Web 消息；v3 中两个值必须等于 calculation_snapshot_revision。
    std::uint64_t parameter_extraction_revision = 0;
    std::uint64_t equipment_result_revision = 0;
    // manifest 参数定义列表（从服务器 manifest.json 传入）
    std::vector<ManifestParam> manifest_params;
};

struct JiavuAutomationResult {
    bool ok = false;
    std::wstring message;
};

JiavuAutomationResult run_jiavu_automation(const JiavuAutomationRequest& request);

}  // namespace cidu_zw3d
