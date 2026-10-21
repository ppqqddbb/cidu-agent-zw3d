# -*- coding: utf-8 -*-
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = [
    Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml"),
    ROOT / "docs" / "参数化设计·文生设计（V2）.yml",
    ROOT / "docs" / "智能体整合.yml",
]

WANTS_FN = (
    "def _wants_calculate(query: str) -> bool:\\n"
    "    return _contains_any(query, (\\\"\u5f00\u59cb\u8ba1\u7b97\\\", \\\"\u516c\u5f0f\u8ba1\u7b97\\\", "
    "\\\"\u8fdb\u884c\u8ba1\u7b97\\\", \\\"\u786e\u8ba4\u8ba1\u7b97\\\"))\\n\\n\\n"
)

REPLACEMENTS = [
    (
        "def _force_explicit_intent(intent:\\\n          \\ dict, query: str, assignments, generic_updates=None):\\n    compact = \\\"\\\n          \\\".join(str(query or \\\"\\\").split())\\n    if assignments:",
        WANTS_FN
        + "def _force_explicit_intent(intent:\\\n          \\ dict, query: str, assignments, generic_updates=None):\\n    compact = \\\"\\\n          \\\".join(str(query or \\\"\\\").split())\\n    if _wants_calculate(query):\\n        intent.update({\\n            \\\"route\\\": \\\"calculate\\\",\\n            \\\"action\\\": \\\"calculate\\\",\\n            \\\"target_table\\\": \\\"param\\\",\\n            \\\"requires_history\\\": True,\\n            \\\"confidence\\\": max(float(intent.get(\\\"confidence\\\", 0) or 0), 0.99),\\n        })\\n        return intent\\n    if assignments:",
    ),
    (
        "    if assignments:\\n        terms\\\n          \\ = [item[\\\"key\\\"] for item in assignments]",
        "    if assignments and not _wants_calculate(query):\\n        terms\\\n          \\ = [item[\\\"key\\\"] for item in assignments]",
    ),
    (
        "          \\      return _payload(\\\"calculate\\\", \\\"\\\", text, query, saved_param, saved_param,\\",
        "          \\      return _payload(\\\"calculate\\\", \\\"\\\", text, original_query, saved_param, saved_param,\\",
    ),
    (
        "        query = _normalize_assignment_command(original_query,\\\n          \\ assignment_updates)\\n    elif generic_last_updates:",
        "        if not _wants_calculate(original_query):\\n            query = _normalize_assignment_command(original_query,\\\n          \\ assignment_updates)\\n    elif generic_last_updates:",
    ),
    (
        "        query = _normalize_result_assignment_command(original_query,\\\n          \\ generic_last_updates)\\n    if calc_result_updates:",
        "        if not _wants_calculate(original_query):\\n            query = _normalize_result_assignment_command(original_query,\\\n          \\ generic_last_updates)\\n    if calc_result_updates:",
    ),
    (
        "    generic_last_updates = [] if assignment_updates else\\\n          \\ _parse_generic_last_table_updates(original_query, saved_last)",
        "    generic_last_updates = _parse_generic_last_table_updates(original_query, saved_last)",
    ),
    # docs yml (unescaped python in yaml string - rare)
    (
        "def _force_explicit_intent(intent: dict, query: str, assignments):\\n    compact = \"\".join(str(query or \"\").split())\\n    if assignments:",
        "def _wants_calculate(query: str) -> bool:\\n    return _contains_any(query, (\"\u5f00\u59cb\u8ba1\u7b97\", \"\u516c\u5f0f\u8ba1\u7b97\", \"\u8fdb\u884c\u8ba1\u7b97\", \"\u786e\u8ba4\u8ba1\u7b97\"))\\n\\n\\ndef _force_explicit_intent(intent: dict, query: str, assignments):\\n    compact = \"\".join(str(query or \"\").split())\\n    if _wants_calculate(query):\\n        intent.update({\"route\": \"calculate\", \"action\": \"calculate\", \"target_table\": \"param\", \"requires_history\": True, \"confidence\": max(float(intent.get(\"confidence\", 0) or 0), 0.99)})\\n        return intent\\n    if assignments:",
    ),
    (
        "    if assignments:\n        terms = [item[\"key\"] for item in assignments]",
        "    if assignments and not _wants_calculate(query):\n        terms = [item[\"key\"] for item in assignments]",
    ),
    (
        'return _payload("calculate", "", text, query, saved_param, saved_param,',
        'return _payload("calculate", "", text, original_query, saved_param, saved_param,',
    ),
    (
        "        query = _normalize_assignment_command(original_query, assignment_updates)\n\n    supplement_query",
        "        if not _wants_calculate(original_query):\n            query = _normalize_assignment_command(original_query, assignment_updates)\n\n    supplement_query",
    ),
]


def patch(text: str) -> str:
    for old, new in REPLACEMENTS:
        if old in text and new not in text:
            text = text.replace(old, new, 1)
    return text


def main() -> None:
    for path in TARGETS:
        if not path.exists():
            print("skip", path)
            continue
        original = path.read_text(encoding="utf-8")
        updated = patch(original)
        if updated == original:
            print("no change:", path.name)
        else:
            path.write_text(updated, encoding="utf-8")
            print("patched:", path)


if __name__ == "__main__":
    main()
