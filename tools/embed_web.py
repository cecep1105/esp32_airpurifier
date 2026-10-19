"""
Kemas web/index.html -> include/web_index.h (gzip, array byte).

Otomatis dijalankan PlatformIO sebelum build (extra_scripts = pre:tools/embed_web.py),
atau manual:  python tools/embed_web.py
Edit tampilan cukup di web/index.html, lalu build/upload seperti biasa.
"""
import gzip
import os

try:
    Import("env")  # noqa: F821  (tersedia saat dijalankan PlatformIO)
    ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SRC = os.path.join(ROOT, "web", "index.html")
DST = os.path.join(ROOT, "include", "web_index.h")


def build():
    with open(SRC, "rb") as f:
        raw = f.read()
    gz = gzip.compress(raw, compresslevel=9, mtime=0)
    lines = []
    for i in range(0, len(gz), 20):
        lines.append("  " + ",".join(str(b) for b in gz[i:i + 20]) + ",")
    out = (
        "// AUTO-GENERATED oleh tools/embed_web.py dari web/index.html - jangan diedit manual\n"
        "#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\n"
        f"// html {len(raw)} B -> gzip {len(gz)} B\n"
        f"static const size_t WEB_INDEX_GZ_LEN = {len(gz)};\n"
        "static const uint8_t WEB_INDEX_GZ[] = {\n" + "\n".join(lines) + "\n};\n"
    )
    old = None
    if os.path.exists(DST):
        with open(DST, "r", encoding="utf-8") as f:
            old = f.read()
    if out != old:  # tulis hanya bila berubah -> tidak memicu rebuild
        with open(DST, "w", encoding="utf-8") as f:
            f.write(out)
        print(f"[embed_web] web/index.html {len(raw)} B -> gzip {len(gz)} B")


build()
