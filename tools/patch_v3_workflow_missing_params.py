from __future__ import annotations

import argparse
import shutil
from datetime import datetime
from pathlib import Path

from ruamel.yaml import YAML


FORMULA_HELPERS = r'''

FORMULA_INPUT_ALIASES = {
    "l1": "comb_plate_length",
    "l3": "boiler_silo_center_distance",
}

FORMULA_EVIDENCE_LABELS = {
    "comb_plate_length": (
        "下联箱梳形板长度",
        "下联箱梳型板长度",
        "锅炉梳型密封板长度",
        "梳形板长度",
        "梳型密封板长度",
    ),
    "comb_plate_width": (
        "下联箱梳形板宽度",
        "下联箱梳型板宽度",
        "锅炉梳型密封板宽度",
        "梳形板宽度",
        "梳型密封板宽度",
    ),
}


def _explicit_formula_evidence_mm(row, labels):
    evidence = _strip((row or {}).get("evidence", ""))
    if not evidence:
        return None
    for label in labels:
        match = re.search(
            re.escape(label) + r"[^0-9+\-]{0,16}([-+]?\d+(?:\.\d+)?)\s*(?:mm|毫米)",
            evidence,
            flags=re.IGNORECASE,
        )
        if match:
            try:
                value = float(match.group(1))
            except Exception:
                continue
            if value > 0:
                return value
    return None


def _repair_formula_input_rows(params):
    # 参数表偶发把 OCR/旧值与知识库证据拼到同一行，例如值为 1900，证据却明确写 19205mm。
    # 仅对有明确“参数名 + 数值 + mm”证据的梳形板尺寸纠正，用户手动赋值不受影响。
    for key, labels in FORMULA_EVIDENCE_LABELS.items():
        row = params.get(key)
        if not row:
            continue
        evidence_value = _explicit_formula_evidence_mm(row, labels)
        if evidence_value is None:
            continue
        current_numbers = _parse_numbers(row.get("value", ""))
        current_value = current_numbers[0] if current_numbers else None
        if current_value is not None and abs(current_value - evidence_value) < 1e-9:
            continue
        old_value = _strip(row.get("value", "")) or "空"
        row["value"] = _fmt_num(evidence_value)
        row["unit"] = "mm"
        note = f"公式输入一致性校正：{old_value}→{_fmt_num(evidence_value)}mm"
        evidence = _strip(row.get("evidence", ""))
        row["evidence"] = evidence + ("；" if evidence else "") + note

    # 新提取模板使用 comb_plate_length / boiler_silo_center_distance；旧备用公式使用 l1 / l3。
    # 在计算入口统一建立别名，避免同一个值因英文 key 不同被误报缺失。
    for alias_key, source_key in FORMULA_INPUT_ALIASES.items():
        alias_row = params.get(alias_key)
        if alias_row and not _is_missing_raw(alias_row.get("value", ""), alias_row.get("evidence", "")):
            continue
        source_row = params.get(source_key)
        if not source_row or _is_missing_raw(source_row.get("value", ""), source_row.get("evidence", "")):
            continue
        cloned = dict(source_row)
        cloned["key"] = alias_key
        cloned["name"] = PARAM_META.get(alias_key, (alias_key, cloned.get("unit", "")))[0]
        evidence = _strip(cloned.get("evidence", ""))
        cloned["evidence"] = evidence + ("；" if evidence else "") + f"参数别名映射：{source_key}→{alias_key}"
        params[alias_key] = cloned
    return params
'''.strip("\n")


MERGE_HELPERS = r'''

CONSISTENCY_EVIDENCE_LABELS = {
    "comb_plate_length": (
        "下联箱梳形板长度",
        "下联箱梳型板长度",
        "锅炉梳型密封板长度",
        "梳形板长度",
        "梳型密封板长度",
    ),
    "comb_plate_width": (
        "下联箱梳形板宽度",
        "下联箱梳型板宽度",
        "锅炉梳型密封板宽度",
        "梳形板宽度",
        "梳型密封板宽度",
    ),
}


def _plain_number(value):
    number = float(value)
    if abs(number - round(number)) < 1e-9:
        return str(int(round(number)))
    return (f"{number:.12f}").rstrip("0").rstrip(".")


def _explicit_consistency_evidence_mm(row, key):
    labels = CONSISTENCY_EVIDENCE_LABELS.get(key, ())
    evidence = _strip((row or {}).get("evidence", ""))
    for label in labels:
        match = re.search(
            re.escape(label) + r"[^0-9+\-]{0,16}([-+]?\d+(?:\.\d+)?)\s*(?:mm|毫米)",
            evidence,
            flags=re.IGNORECASE,
        )
        if match:
            try:
                value = float(match.group(1))
            except Exception:
                continue
            if value > 0:
                return value
    return None


def _repair_value_evidence_consistency(rows):
    repaired = []
    for source_row in rows or []:
        row = dict(source_row)
        key = row.get("key", "")
        evidence_value = _explicit_consistency_evidence_mm(row, key)
        if evidence_value is not None:
            current_values = re.findall(r"[-+]?\d+(?:\.\d+)?", _strip(row.get("value", "")).replace(",", ""))
            current_value = float(current_values[0]) if current_values else None
            if current_value is None or abs(current_value - evidence_value) >= 1e-9:
                old_value = _strip(row.get("value", "")) or "空"
                row["value"] = _plain_number(evidence_value)
                row["unit"] = "mm"
                evidence = _strip(row.get("evidence", ""))
                note = f"参数值按同条证据校正：{old_value}→{_plain_number(evidence_value)}mm"
                row["evidence"] = evidence + ("；" if evidence else "") + note
        repaired.append(row)
    return repaired


def _prefer_explicit_supplement_value(key, current_row, supplement_row):
    evidence_value = _explicit_consistency_evidence_mm(supplement_row, key)
    if evidence_value is None:
        return False
    supplement_values = re.findall(r"[-+]?\d+(?:\.\d+)?", _strip(supplement_row.get("value", "")).replace(",", ""))
    if not supplement_values or abs(float(supplement_values[0]) - evidence_value) >= 1e-9:
        return False
    current_values = re.findall(r"[-+]?\d+(?:\.\d+)?", _strip(current_row.get("value", "")).replace(",", ""))
    return not current_values or abs(float(current_values[0]) - evidence_value) >= 1e-9
'''.strip("\n")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_formula_code(code: str) -> str:
    if "FORMULA_INPUT_ALIASES" not in code:
        anchor = "    return rows\n\n\nclass FormulaContext:"
        replacement = "    return rows\n\n\n" + FORMULA_HELPERS + "\n\n\nclass FormulaContext:"
        code = replace_once(code, anchor, replacement, "formula helpers")

    old_main = "    params = _parse_param_table(markdown)\n    selected_keys = _select_skills(user_query)"
    new_main = "    params = _repair_formula_input_rows(_parse_param_table(markdown))\n    selected_keys = _select_skills(user_query)"
    if old_main in code:
        code = replace_once(code, old_main, new_main, "formula main normalization")
    elif new_main not in code:
        raise RuntimeError("formula main normalization anchor missing")

    old_fallback = '''    # 备用计算逻辑：使用L1、L3、D1等参数
    capacity = ctx.get_number("boiler_capacity", impact)
    rate = ctx.get_number("max_slag_rate", impact)
    l1 = ctx.get_number("l1", impact)
    l3 = ctx.get_number("l3", impact)
    d1 = ctx.get_number("d1", impact)'''
    new_fallback = '''    # 备用计算逻辑：兼容新模板 key，避免已有尺寸因旧 key l1/l3 被误报缺失。
    capacity = ctx.get_number("boiler_capacity", impact)
    rate = ctx.get_number("max_slag_rate", impact)
    l1 = ctx.get_number("comb_plate_length", impact, required=False)
    if l1 is None:
        l1 = ctx.get_number("l1", impact)
    l3 = center_distance
    if l3 is None:
        l3 = ctx.get_number("l3", impact)
    d1 = ctx.get_number("d1", impact)'''
    if old_fallback in code:
        code = replace_once(code, old_fallback, new_fallback, "conveyor fallback aliases")
    elif new_fallback not in code:
        raise RuntimeError("conveyor fallback anchor missing")
    return code


def patch_merge_code(code: str) -> str:
    if "CONSISTENCY_EVIDENCE_LABELS" not in code:
        anchor = "    return {\"rows\": rows, \"by_key\": by_key, \"headers\": headers}\n\ndef _format_table(rows):"
        replacement = (
            "    return {\"rows\": rows, \"by_key\": by_key, \"headers\": headers}\n\n\n"
            + MERGE_HELPERS
            + "\n\ndef _format_table(rows):"
        )
        code = replace_once(code, anchor, replacement, "merge consistency helpers")

    old_base = '''    base = _parse_table(base_markdown, base_source)
    rows = [dict(row) for row in base["rows"]]

    supplement = _parse_table(kb_param_markdown, "知识库全文")'''
    new_base = '''    base = _parse_table(base_markdown, base_source)
    base["rows"] = _repair_value_evidence_consistency(base["rows"])
    base["by_key"] = {row["key"]: row for row in base["rows"]}
    rows = [dict(row) for row in base["rows"]]

    supplement = _parse_table(kb_param_markdown, "知识库全文")
    supplement["rows"] = _repair_value_evidence_consistency(supplement["rows"])
    supplement["by_key"] = {row["key"]: row for row in supplement["rows"]}'''
    if old_base in code:
        code = replace_once(code, old_base, new_base, "merge input consistency")
    elif new_base not in code:
        raise RuntimeError("merge input consistency anchor missing")

    old_condition = '''            if _is_missing(row_a.get("value"), row_a.get("evidence")):
                merged = dict(row_a)'''
    new_condition = '''            if (_is_missing(row_a.get("value"), row_a.get("evidence"))
                    or _prefer_explicit_supplement_value(key, row_a, row_b)):
                merged = dict(row_a)'''
    if old_condition in code:
        code = replace_once(code, old_condition, new_condition, "supplement preference")
    elif new_condition not in code:
        raise RuntimeError("supplement preference anchor missing")

    old_user = "    rows = _apply_user_supplements(rows, user_query)\n    rows = _complete_template_rows(rows)"
    new_user = "    rows = _repair_value_evidence_consistency(rows)\n    rows = _apply_user_supplements(rows, user_query)\n    rows = _complete_template_rows(rows)"
    if old_user in code:
        code = replace_once(code, old_user, new_user, "final consistency repair")
    elif new_user not in code:
        raise RuntimeError("final consistency anchor missing")
    return code


def find_node(document, node_id: str):
    for node in document["workflow"]["graph"]["nodes"]:
        if node.get("id") == node_id:
            return node
    raise RuntimeError(f"workflow node not found: {node_id}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("workflow", type=Path)
    parser.add_argument("--no-backup", action="store_true")
    args = parser.parse_args()

    workflow_path = args.workflow.resolve()
    yaml = YAML(typ="rt")
    yaml.preserve_quotes = True
    yaml.width = 1_000_000
    with workflow_path.open("r", encoding="utf-8") as stream:
        document = yaml.load(stream)

    formula_node = find_node(document, "formula_calculation_node")
    merge_node = find_node(document, "param_autocomplete_merge_node")
    formula_code = patch_formula_code(str(formula_node["data"]["code"]))
    merge_code = patch_merge_code(str(merge_node["data"]["code"]))
    compile(formula_code, "formula_calculation_node", "exec")
    compile(merge_code, "param_autocomplete_merge_node", "exec")
    formula_node["data"]["code"] = formula_code
    merge_node["data"]["code"] = merge_code

    backup_path = None
    if not args.no_backup:
        stamp = datetime.now().strftime("%Y%m%d%H%M%S")
        backup_path = workflow_path.with_name(workflow_path.name + f".bak.{stamp}")
        shutil.copy2(workflow_path, backup_path)

    temp_path = workflow_path.with_name(workflow_path.name + ".tmp")
    with temp_path.open("w", encoding="utf-8", newline="\n") as stream:
        yaml.dump(document, stream)
    with temp_path.open("r", encoding="utf-8") as stream:
        verify_document = yaml.load(stream)
    verify_formula = str(find_node(verify_document, "formula_calculation_node")["data"]["code"])
    verify_merge = str(find_node(verify_document, "param_autocomplete_merge_node")["data"]["code"])
    compile(verify_formula, "formula_calculation_node", "exec")
    compile(verify_merge, "param_autocomplete_merge_node", "exec")
    temp_path.replace(workflow_path)

    print(f"patched={workflow_path}")
    if backup_path:
        print(f"backup={backup_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
