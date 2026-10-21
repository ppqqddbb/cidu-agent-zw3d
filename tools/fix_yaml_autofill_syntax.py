# -*- coding: utf-8 -*-
"""Fix broken autofill patch in Dify workflow YAML files."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = [
    Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml"),
    ROOT / "docs" / "参数化设计·文生设计（V2）.yml",
    ROOT / "docs" / "智能体整合.yml",
]

# Broken patch left invalid Python: `if not is_shared = ...`
BROKEN = (
    "        if not\\\n"
    "          \\ is_shared = _cidu_is_shared_construction(name)\\n        if not is_shared and not _cidu_autofill_matches(name, keywords):\\n            continue\\n      \\\n"
    "          \\  param_name = name if is_shared else _cidu_autofill_param_name(name, keywords)"
)

FIXED = (
    "        is_shared = _cidu_is_shared_construction(name)\\n        if not is_shared and not _cidu_autofill_matches(name, keywords):\\n            continue\\n      \\\n"
    "          \\  param_name = name if is_shared else _cidu_autofill_param_name(name, keywords)"
)

# Also handle if someone already partially fixed one line variant
BROKEN_ALT = (
    "if not is_shared = _cidu_is_shared_construction(name)\\n        if not is_shared and not _cidu_autofill_matches(name, keywords):"
)
FIXED_ALT = (
    "is_shared = _cidu_is_shared_construction(name)\\n        if not is_shared and not _cidu_autofill_matches(name, keywords):"
)


def patch_file(path: Path) -> bool:
    if not path.exists():
        print("skip missing:", path)
        return False
    text = path.read_text(encoding="utf-8")
    original = text
    text = text.replace(BROKEN, FIXED)
    text = text.replace(BROKEN_ALT, FIXED_ALT)
    if text == original:
        if "if not is_shared = _cidu_is_shared_construction" in text:
            print("still broken, manual fix needed:", path)
            return False
        print("already ok:", path)
        return False
    path.write_text(text, encoding="utf-8")
    print("fixed:", path)
    return True


def main() -> None:
    changed = sum(1 for target in TARGETS if patch_file(target))
    print("done,", changed, "file(s) fixed")


if __name__ == "__main__":
    main()
