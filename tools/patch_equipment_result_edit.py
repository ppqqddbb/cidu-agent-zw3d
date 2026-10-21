# -*- coding: utf-8 -*-
"""Patch workflow YAML: support equipment result table edits on recalculate."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = [
    Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml"),
    ROOT / "docs" / "参数化设计·文生设计（V2）.yml",
    ROOT / "docs" / "智能体整合.yml",
]

OVERRIDE_HELPERS = (
    "def _parse_selection_result_overrides(query: str):\\n"
    "    overrides = {}\\n"
    "    for raw_line in str(query or \\\"\\\").splitlines():\\n"
    "        line = raw_line.strip()\\n"
    "        if not line:\\n"
    "            continue\\n"
    "        if re.fullmatch(r\\\"(?:开始计算|公式计算|进行计算|确认计算)\\\", line.replace(\\\" \\\", \\\"\\\")):\\n"
    "            continue\\n"
    "        key = value = \\\"\\\"\\n"
    "        for sep in (\\\"=\\\", \\\"：\\\", \\\":\\\"):\\n"
    "            if sep in line:\\n"
    "                key, value = line.split(sep, 1)\\n"
    "                break\\n"
    "        if not key:\\n"
    "            parts = re.split(r\\\"\\\\s+\\\", line, maxsplit=1)\\n"
    "            if len(parts) == 2:\\n"
    "                key, value = parts\\n"
    "        key = str(key or \\\"\\\").strip()\\n"
    "        value = str(value or \\\"\\\").strip()\\n"
    "        if key and value:\\n"
    "            overrides[key] = value\\n"
    "    return overrides\\n\\n\\n"
    "def _apply_selection_result_overrides(rows, query: str):\\n"
    "    overrides = _parse_selection_result_overrides(query)\\n"
    "    if not overrides:\\n"
    "        return rows\\n"
    "    applied = []\\n"
    "    for row in rows:\\n"
    "        label = row[0]\\n"
    "        if label in overrides:\\n"
    "            applied.append([label, overrides[label], row[2] if len(row) > 2 else \\\"\\\"])\\n"
    "        else:\\n"
    "            applied.append(row)\\n"
    "    return applied\\n\\n\\n"
)

FIXED_RESULT_OLD = (
    "result_table\\n          \\ = _markdown_table([\\\"参数\\\", \\\"参数值\\\", \\\"后续应用\\\"], "
    "_fixed_equipment_selection_rows())\\n"
)
FIXED_RESULT_NEW = (
    "selection_result_rows = _apply_selection_result_overrides(_fixed_equipment_selection_rows(), ctx.query)\\n"
    "    result_table\\n          \\ = _markdown_table([\\\"参数\\\", \\\"参数值\\\", \\\"后续应用\\\"], "
    "selection_result_rows)\\n"
)

DYNAMIC_RESULT_OLD = (
    "    if selection_rows:\\n        result_table = _markdown_table([\\\"参数\\\", \\\"参数值\\\", \\\"后续应用\\\"], "
    "selection_rows)\\n"
)
DYNAMIC_RESULT_NEW = (
    "    overrides = _parse_selection_result_overrides(ctx.query)\\n"
    "    if overrides:\\n        for idx, row in enumerate(selection_rows):\\n"
    "            if row and row[0] in overrides:\\n"
    "                selection_rows[idx] = [row[0], overrides[row[0]], row[2] if len(row) > 2 else \\\"\\\"]\\n"
    "    if selection_rows:\\n        result_table = _markdown_table([\\\"参数\\\", \\\"参数值\\\", \\\"后续应用\\\"], "
    "selection_rows)\\n"
)

CALC_PRIORITY_OLD = (
    "        return intent\\n    generic_updates = generic_updates or []\\n    if generic_updates:\\n"
)
CALC_PRIORITY_NEW = (
    "        return intent\\n    if _contains_any(query, (\\\"开始计算\\\", \\\"公式计算\\\", \\\"进行计算\\\", "
    "\\\"确认计算\\\")):\\n        intent.update({\\n            \\\"route\\\": \\\"calculate\\\",\\n"
    "            \\\"action\\\": \\\"calculate\\\",\\n            \\\"target_table\\\": \\\"param\\\",\\n"
    "            \\\"requires_history\\\": True,\\n            \\\"confidence\\\": max(float(intent.get(\\\"confidence\\\", 0) or 0), 0.99),\\n"
    "        })\\n        return intent\\n    generic_updates = generic_updates or []\\n    if generic_updates:\\n"
)


def patch_file(path: Path) -> bool:
    if not path.exists():
        print("skip missing:", path)
        return False
    text = path.read_text(encoding="utf-8")
    original = text

    if "_parse_selection_result_overrides" not in text:
        anchor = "def _build_output(ctx, selected_keys):"
        if anchor in text:
            text = text.replace(anchor, OVERRIDE_HELPERS + anchor, 1)

    if FIXED_RESULT_OLD in text:
        text = text.replace(FIXED_RESULT_OLD, FIXED_RESULT_NEW, 1)

    if DYNAMIC_RESULT_OLD in text:
        text = text.replace(DYNAMIC_RESULT_OLD, DYNAMIC_RESULT_NEW, 1)

    if CALC_PRIORITY_OLD in text and CALC_PRIORITY_NEW.split("generic_updates")[0] not in text:
        text = text.replace(CALC_PRIORITY_OLD, CALC_PRIORITY_NEW, 1)

    if text == original:
        print("already ok:", path.name)
        return False
    path.write_text(text, encoding="utf-8")
    print("patched:", path)
    return True


def main() -> None:
    changed = sum(1 for target in TARGETS if patch_file(target))
    print("done,", changed, "file(s) updated")


if __name__ == "__main__":
    main()
