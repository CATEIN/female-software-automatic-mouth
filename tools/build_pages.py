"""Assembles the GitHub Pages site in _site/ (run by .github/workflows/pages.yml).

    python tools/build_pages.py

  _site/index.html            the voice player (web/female_sam.html)
  _site/blindtest/            the blind listening test with its clips
The blind test is written as a claude.ai artifact body, so it gets the same
document wrapper the artifact host adds. Its pooled tally only works on
claude.ai; on Pages each listener sees their own score.
"""
import os, shutil

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SITE = os.path.join(ROOT, "_site")
WRAP = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
        '<style>:root{color-scheme:light}body{margin:0}[hidden]{display:none!important}</style>\n'
        '</head>\n<body>\n')

shutil.rmtree(SITE, ignore_errors=True)
os.makedirs(os.path.join(SITE, "blindtest"))
shutil.copy(os.path.join(ROOT, "web", "female_sam.html"), os.path.join(SITE, "index.html"))
src = os.path.join(ROOT, "web", "blindtest")
with open(os.path.join(src, "index.html"), encoding="utf-8") as f:
    body = f.read()
with open(os.path.join(SITE, "blindtest", "index.html"), "w", encoding="utf-8") as f:
    f.write(WRAP + body + "\n</body>\n</html>\n")
shutil.copy(os.path.join(src, "clips.json"), os.path.join(SITE, "blindtest", "clips.json"))
shutil.copytree(os.path.join(src, "audio"), os.path.join(SITE, "blindtest", "audio"))
open(os.path.join(SITE, ".nojekyll"), "w").close()
print("built", SITE)
