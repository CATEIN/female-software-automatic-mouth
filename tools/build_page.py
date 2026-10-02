"""Builds the self-contained web page from web/page.template.html.

    build_web.bat              # C -> web/sam.wasm first
    python tools/build_page.py

Inlines web/sam_loader.js and web/sam.wasm (base64) and writes:
  web/female_sam.html   standalone page: open it straight from disk
  web/artifact.html     the same content without the html/head/body wrapper,
                        as published to claude.ai
"""
import base64
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")


def main():
    template = open(os.path.join(WEB, "page.template.html"), encoding="utf-8").read()
    loader = open(os.path.join(WEB, "sam_loader.js"), encoding="utf-8").read()
    wasm = base64.b64encode(open(os.path.join(WEB, "sam.wasm"), "rb").read()).decode("ascii")
    assert "/*SAM_LOADER*/" in template and "__SAM_WASM_BASE64__" in template
    page = template.replace("/*SAM_LOADER*/", loader).replace("__SAM_WASM_BASE64__", wasm)

    with open(os.path.join(WEB, "artifact.html"), "w", encoding="utf-8") as f:
        f.write(page)
    standalone = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
                  '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
                  '<style>:root{color-scheme:light}body{margin:0}[hidden]{display:none!important}</style>\n'
                  '</head>\n<body>\n' + page + '\n</body>\n</html>\n')
    with open(os.path.join(WEB, "female_sam.html"), "w", encoding="utf-8") as f:
        f.write(standalone)
    print(f"web/female_sam.html and web/artifact.html written ({len(standalone) / 1024:.0f} KB, wasm {len(wasm) * 3 // 4 // 1024} KB)")


if __name__ == "__main__":
    main()
