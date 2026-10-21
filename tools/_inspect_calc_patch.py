# -*- coding: utf-8 -*-
from pathlib import Path

p = Path(r"D:\InstallationPackage\智能出图（v1.0） (62).yml")
t = p.read_text(encoding="utf-8")
for needle in ["if assignment_updates:", "return _payload(\"calculate\"", "_force_explicit_intent", "generic_last_updates = []"]:
    i = t.find(needle)
    print("===", needle, i)
    if i >= 0:
        print(repr(t[i : i + 350]))
