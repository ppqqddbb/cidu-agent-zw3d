# -*- coding: utf-8 -*-
from pathlib import Path
t = Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml").read_text(encoding="utf-8")
i = t.find("calculate")
while i >= 0 and i < 82000:
    if "_payload" in t[i:i+80] and "calculate" in t[i:i+80]:
        print(repr(t[i:i+180]))
        break
    i = t.find("calculate", i+1)
# find all _payload calculate
import re
for m in re.finditer(r'.{0,30}_payload.{0,80}calculate.{0,80}', t):
    print('MATCH', repr(m.group(0)[:200]))
