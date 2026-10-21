# -*- coding: utf-8 -*-
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
docs = [ROOT / "docs" / "智能体整合.yml", ROOT / "docs" / "参数化设计·文生设计（V2）.yml"]

WANTS = (
    "def _wants_calculate(query: str) -> bool:\\n"
    "    return _contains_any(query, (\\\"\u5f00\u59cb\u8ba1\u7b97\\\", \\\"\u516c\u5f0f\u8ba1\u7b97\\\", "
    "\\\"\u8fdb\u884c\u8ba1\u7b97\\\", \\\"\u786e\u8ba4\u8ba1\u7b97\\\"))\\n\\n\\n"
)

REPL = [
    (
        "def _force_explicit_intent(intent:\\\n          \\ dict, query: str, assignments):\\n    compact = \\\"\\\".join(str(query or\\\n          \\ \\\"\\\").split())\\n    if assignments:\\n        terms = [item[\\\"key\\\"] for\\\n          \\ item in assignments]",
        WANTS
        + "def _force_explicit_intent(intent:\\\n          \\ dict, query: str, assignments):\\n    compact = \\\"\\\".join(str(query or\\\n          \\ \\\"\\\").split())\\n    if _wants_calculate(query):\\n        intent.update({\\n            \\\"route\\\": \\\"calculate\\\",\\n            \\\"action\\\": \\\"calculate\\\",\\n            \\\"target_table\\\": \\\"param\\\",\\n            \\\"requires_history\\\": True,\\n            \\\"confidence\\\": max(float(intent.get(\\\"confidence\\\", 0) or 0), 0.99),\\n        })\\n        return intent\\n    if assignments and not _wants_calculate(query):\\n        terms = [item[\\\"key\\\"] for\\\n          \\ item in assignments]",
    ),
    (
        "          \\      return _payload(\\\"calculate\\\", \\\"\\\", text, query, saved_param, saved_param,\\",
        "          \\      return _payload(\\\"calculate\\\", \\\"\\\", text, original_query, saved_param, saved_param,\\",
    ),
    (
        "        query = _normalize_assignment_command(original_query,\\\n          \\ assignment_updates)\\n\\n    supplement_query",
        "        if not _wants_calculate(original_query):\\n            query = _normalize_assignment_command(original_query,\\\n          \\ assignment_updates)\\n\\n    supplement_query",
    ),
]

for path in docs:
    text = path.read_text(encoding="utf-8")
    orig = text
    for old, new in REPL:
        if old in text:
            text = text.replace(old, new, 1)
    if text != orig:
        path.write_text(text, encoding="utf-8")
        print("patched", path.name)
    else:
        print("no change", path.name)
