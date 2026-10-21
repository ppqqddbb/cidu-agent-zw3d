# -*- coding: utf-8 -*-
"""Patch Dify workflow YAML: fix equipment table autofill for new table headers."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = [
    Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml"),
    ROOT / "docs" / "参数化设计·文生设计（V2）.yml",
    ROOT / "docs" / "智能体整合.yml",
]

REPLACEMENTS = [
    (
        r'(\"设备名称\", \"设备型号/参数\", \"取得途径\", \"参数标识\"\\'
        r'\n          , \"数值\")',
        r'(\"参数\", \"参数值\", \"后续应用\", \"设备名称\", \"设备型号/参数\", \"取得途径\", \"参数标识\"\\'
        r'\n          , \"数值\")',
    ),
    (
        r'[\"设备名称\", \"参数中文名称\", \"参数名称\"\\'
        r'\n          , \"参数标识\", \"名称\", \"字段\", \"参数名\"]',
        r'[\"参数\", \"设备名称\", \"参数中文名称\", \"参数名称\"\\'
        r'\n          , \"参数标识\", \"名称\", \"字段\", \"参数名\"]',
    ),
    (
        "        if not _cidu_autofill_matches(name, keywords):\\n            continue\\n      \\\n"
        "          \\  param_name = _cidu_autofill_param_name(name, keywords)",
        "        is_shared = _cidu_is_shared_construction(name)\\n        if not is_shared and not _cidu_autofill_matches(name, keywords):\\n            continue\\n      \\\n"
        "          \\  param_name = name if is_shared else _cidu_autofill_param_name(name, keywords)",
    ),
]

SHARED = [
    "\u6e23\u4ed3\u76f4\u5f84", "\u6e23\u4ed3\u9876\u6807\u9ad8", "\u6e23\u4ed3\u9525\u6bb5\u9ad8\u5ea6",
    "\u6e23\u4ed3\u67f1\u6bb5\u9ad8\u5ea6", "\u6e23\u4ed3\u6709\u6548\u5bb9\u79ef", "\u6e23\u4ed3\u5378\u6599\u7c7b\u578b",
    "\u6e23\u4e95\u4e0b\u53e3\u6807\u9ad8", "\u6e23\u4e95\u58c1\u677f\u4e0a\u53e3\u6807\u9ad8", "\u6e23\u4e95\u58c1\u677f\u4e0a\u53e3\u957f\u5ea6",
    "\u6e23\u4e95\u5de6\u53f3\u58c1\u677f\u89d2\u5ea6", "\u5173\u65ad\u95e8\u53e3\u95f4\u8ddd", "\u5173\u65ad\u95e8\u5b9a\u4f4d\u8ddd\u79bb",
    "\u5173\u65ad\u95e8\u7bb1\u4f53\u957f\u5ea6", "\u5173\u65ad\u95e8\u7bb1\u4f53\u9ad8\u5ea6", "\u5173\u65ad\u95e8\u7bb1\u4f53\u5bbd\u5ea6",
    "\u81a8\u80c0\u8282\u957f\u5ea6", "\u81a8\u80c0\u8282\u5bbd\u5ea6", "\u81a8\u80c0\u8282\u63a5\u53e3\u957f\u5ea6", "\u81a8\u80c0\u8282\u63a5\u53e3\u5bbd\u5ea6",
]
SHARED_ITEMS = ", ".join(f'\\"{item}\\"' for item in SHARED)


def build_shared_fn() -> str:
    return (
        "def _cidu_is_shared_construction(name: str) -> bool:\\n"
        "    shared = [\\n"
        f"        {SHARED_ITEMS},\\n"
        "    ]\\n"
        "    name_norm = _cidu_autofill_norm(name)\\n"
        "    return any(\\n"
        "        name_norm == _cidu_autofill_norm(item)\\n"
        "        or name_norm in _cidu_autofill_norm(item)\\n"
        "        or _cidu_autofill_norm(item) in name_norm\\n"
        "        for item in shared\\n"
        "    )\\n\\n\\n"
    )


def patch_file(path: Path) -> bool:
    if not path.exists():
        print("skip missing:", path)
        return False
    text = path.read_text(encoding="utf-8")
    original = text
    for old, new in REPLACEMENTS:
        text = text.replace(old, new)
    if "_cidu_is_shared_construction" not in text:
        anchor = "def _cidu_autofill_parameters(result: dict, signals: dict) -> dict:"
        if anchor in text:
            text = text.replace(anchor, build_shared_fn() + anchor, 1)
    if text == original:
        print("no changes:", path)
        return False
    path.write_text(text, encoding="utf-8")
    print("patched:", path)
    return True


def main() -> None:
    changed = sum(1 for target in TARGETS if patch_file(target))
    print("done,", changed, "file(s) updated")


if __name__ == "__main__":
    main()
