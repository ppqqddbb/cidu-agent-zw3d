# -*- coding: utf-8 -*-
from pathlib import Path

p = Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml")
text = p.read_text(encoding="utf-8")
original = text

HELPERS = (
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
)

ANCHOR = "def _build_output(ctx,\\\n          \\ selected_keys):"
if "_parse_selection_result_overrides" not in text and ANCHOR in text:
    text = text.replace(ANCHOR, HELPERS + ANCHOR, 1)

OLD = (
    "    \\n    if selection_rows:\\n        result_table = _markdown_table([\\\"\\\n"
    "          参数\\\", \\\"参数值\\\", \\\"后续应用\\\"], selection_rows)\\n"
)
NEW = (
    "    \\n    overrides = _parse_selection_result_overrides(ctx.query)\\n"
    "    if overrides:\\n        for idx, row in enumerate(selection_rows):\\n"
    "            if row and row[0] in overrides:\\n"
    "                selection_rows[idx] = [row[0], overrides[row[0]], row[2] if len(row) > 2 else \\\"\\\"]\\n"
    "    if selection_rows:\\n        result_table = _markdown_table([\\\"\\\n"
    "          参数\\\", \\\"参数值\\\", \\\"后续应用\\\"], selection_rows)\\n"
)
if OLD in text:
    text = text.replace(OLD, NEW, 1)

CALC_OLD = (
    "        return intent\\n\\\n          \\    generic_updates = generic_updates or []\\n    if generic_updates:\\n"
)
CALC_NEW = (
    "        return intent\\n    if _contains_any(query, (\\\"开始计算\\\", \\\"公式计算\\\", \\\"进行计算\\\", \\\"确认计算\\\")):\\n"
    "        intent.update({\\n            \\\"route\\\": \\\"calculate\\\",\\n"
    "            \\\"action\\\": \\\"calculate\\\",\\n            \\\"target_table\\\": \\\"param\\\",\\n"
    "            \\\"requires_history\\\": True,\\n            \\\"confidence\\\": max(float(intent.get(\\\"confidence\\\", 0) or 0), 0.99),\\n"
    "        })\\n        return intent\\n\\\n          \\    generic_updates = generic_updates or []\\n    if generic_updates:\\n"
)
if CALC_OLD in text and "route\\\": \\\"calculate\\\",\\n            \\\"action\\\": \\\"calculate\\\"" not in text[text.find(CALC_OLD)-200:text.find(CALC_OLD)+400]:
    text = text.replace(CALC_OLD, CALC_NEW, 1)

if text == original:
    print("no changes")
else:
    p.write_text(text, encoding="utf-8")
    print("patched", p)
