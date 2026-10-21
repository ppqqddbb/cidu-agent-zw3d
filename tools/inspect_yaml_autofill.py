# -*- coding: utf-8 -*-
from pathlib import Path
import re

p = Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml")
text = p.read_text(encoding="utf-8")

broken = "if not is_shared = _cidu_is_shared_construction"
print("broken count:", text.count(broken))

idx = text.find("_cidu_autofill_parameters")
if idx >= 0:
    chunk = text[idx : idx + 5000]
    pos = chunk.find("for row in")
    if pos >= 0:
        segment = chunk[pos : pos + 1200]
        print("RAW SEGMENT:")
        print(segment[:800])

# Try compile extracted python from one code node
for m in re.finditer(r"def _cidu_is_shared_construction[\s\S]{0,4000}?return parameters", text):
    code = m.group(0)
    code = code.encode("utf-8").decode("unicode_escape")
    code = code.replace("\\n", "\n")
    try:
        compile(code + "\n    return parameters", "<test>", "exec")
        print("compile ok")
    except SyntaxError as e:
        print("SYNTAX ERROR:", e)
        print("line:", e.lineno)
        lines = (code + "\n    return parameters").split("\n")
        if e.lineno:
            for i in range(max(0, e.lineno - 3), min(len(lines), e.lineno + 2)):
                print(f"{i+1}: {lines[i]}")
