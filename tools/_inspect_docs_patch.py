# -*- coding: utf-8 -*-
from pathlib import Path
for name in ["智能体整合.yml", "参数化设计·文生设计（V2）.yml"]:
    t = Path(r"D:\File\VSCodeFile\QDHB\cidu-agent-zw3d\docs") / name
    text = t.read_text(encoding="utf-8")
    i = text.find("_force_explicit_intent")
    print(name, repr(text[i:i+250]))
    j = text.find("route == \"calculate\"")
    if j < 0:
        j = text.find('route == \\"calculate\\"')
    print("calc", j, repr(text[j:j+150]) if j>=0 else "none")
