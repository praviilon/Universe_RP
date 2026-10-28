#!/usr/bin/env python3
# TEST ONLY: the menu files' value lists (showCvar, hideCvar, enableCvar, disableCvar). The engine's menu reader joins
# string literals that stand side by side, as C does, so { "2" "3" } is the one value "23": every value must be
# separated from the next by ';' ({ "2" ; "3" }). The dialogue menu lost its options 2-5 to this once.
import glob, os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
lists = re.compile(r'(?i)\b(showCvar|hideCvar|enableCvar|disableCvar)\s*\{([^}]*)\}')
adjacent = re.compile(r'"[^"\n]*"\s+"')
bad = 0
files = glob.glob(os.path.join(root, 'assets', '**', '*.menu'), recursive=True)
for path in files:
    text = open(path, encoding='latin-1').read()
    for m in lists.finditer(text):
        body = re.sub(r'//[^\n]*', '', m.group(2))
        if adjacent.search(body):
            line = text.count('\n', 0, m.start()) + 1
            print('FAIL %s:%d: %s { %s } -- separate the values with ;' % (os.path.relpath(path, root), line, m.group(1), ' '.join(body.split())))
            bad += 1
if not files:
    print('FAIL no .menu files found under assets/')
    bad += 1
print('%d FAILED' % bad if bad else 't_menulint: ALL OK (%d menu files)' % len(files))
sys.exit(1 if bad else 0)
