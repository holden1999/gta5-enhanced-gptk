#!/usr/bin/env python3
"""Set the per-app LaunchOptions entry in Steam's localconfig.vdf.

Edits only the app's own block: the file is scanned with a brace matcher, so a
value in another app is never touched. Steam must be closed when writing.

  set-launch-options.py --appid 3240220 --check
  set-launch-options.py --appid 3240220 --value 'CX_GRAPHICS_BACKEND=d3dmetal %command%'
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

STEAM = os.environ.get("STEAM_SUPPORT", os.path.expanduser("~/Library/Application Support/Steam"))


def find_localconfig(appid):
    paths = glob.glob(os.path.join(STEAM, "userdata", "*", "config", "localconfig.vdf"))
    if not paths:
        return None
    for p in paths:
        try:
            if '"%s"' % appid in open(p, encoding="utf-8", errors="replace").read():
                return p
        except OSError:
            continue
    return paths[0]


def skip_string(text, i):
    """i points at an opening quote; return the index after the closing quote."""
    i += 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
            continue
        if text[i] == '"':
            return i + 1
        i += 1
    return i


def find_app_block(text, appid):
    key = '"%s"' % appid
    pos = 0
    while True:
        k = text.find(key, pos)
        if k < 0:
            return None
        j = k + len(key)
        while j < len(text) and text[j] in " \t\r\n":
            j += 1
        if j < len(text) and text[j] == "{":
            depth = 0
            i = j
            while i < len(text):
                c = text[i]
                if c == '"':
                    i = skip_string(text, i)
                    continue
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        return j, i + 1
                i += 1
            return None
        pos = k + len(key)


def current_value(block):
    m = re.search(r'"LaunchOptions"\s*"((?:[^"\\]|\\.)*)"', block)
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--appid", required=True)
    ap.add_argument("--value")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    path = find_localconfig(args.appid)
    if not path:
        print("no localconfig.vdf found under %s/userdata" % STEAM)
        return 1

    text = open(path, encoding="utf-8", errors="replace").read()
    found = find_app_block(text, args.appid)
    if not found:
        print("app %s has no block in %s; launch the game once first" % (args.appid, path))
        return 1
    start, end = found
    block = text[start:end]
    have = current_value(block)

    if args.check or args.value is None:
        print("LaunchOptions for %s: %s" % (args.appid, have if have else "(unset)"))
        return 0

    if have == args.value:
        print("LaunchOptions for %s already set" % args.appid)
        return 0

    if subprocess.run(["pgrep", "-f", "Steam.app/Contents/MacOS/steam_osx"],
                      stdout=subprocess.DEVNULL).returncode == 0:
        print("Steam is running; close it and run this again", file=sys.stderr)
        print("manual: Properties > General > Launch Options > %s" % args.value, file=sys.stderr)
        return 3

    quoted = args.value.replace("\\", "\\\\").replace('"', '\\"')
    if have is None:
        new_block = block[:1] + '\n\t\t\t\t\t\t"LaunchOptions"\t\t"%s"' % quoted + block[1:]
    else:
        new_block = re.sub(r'("LaunchOptions"\s*")(?:[^"\\]|\\.)*(")', r"\g<1>%s\g<2>" % quoted.replace("\\", "\\\\"), block, count=1)

    if not os.path.exists(path + ".gptk-bak"):
        shutil.copy2(path, path + ".gptk-bak")
    open(path, "w", encoding="utf-8").write(text[:start] + new_block + text[end:])
    print("LaunchOptions for %s: %s -> %s" % (args.appid, have, args.value))
    return 0


if __name__ == "__main__":
    sys.exit(main())
