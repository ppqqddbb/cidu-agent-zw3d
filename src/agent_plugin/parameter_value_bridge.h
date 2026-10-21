#pragma once

#include "jiavu_automation.h"

#include <cstddef>

namespace cidu_zw3d {

struct ParameterValueBridgeReport {
    std::size_t extraction_count = 0;
    std::size_t equipment_count = 0;
    std::size_t target_count = 0;
    std::size_t matched_equipment = 0;
    std::size_t matched_extraction = 0;
    std::size_t manual_overrides_applied = 0;
    std::size_t unmatched = 0;
};

// parameter_extraction_rows 与 equipment_result_rows 必须来自同一条最新“开始计算”响应，
// 再按参数名称匹配 manifest_params；同名时采用设备选型结果中的最终计算值。
// 设计模型参数表中用户明确手动修改的值最后应用，优先级高于计算快照。
ParameterValueBridgeReport apply_parameter_value_bridge(JiavuAutomationRequest& request);

}  // namespace cidu_zw3d
