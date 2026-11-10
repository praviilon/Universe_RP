#!/usr/bin/env python3
# TEST ONLY: the mod's name in the code's texts. The player-facing name is RP_MOD_NAME (galaxyrp/game/rp_version.h),
# so a rename is one line: a text writes it as "... " RP_MOD_NAME " ...", never typed out. This fails on any string
# literal in the game, cgame and ui code (and galaxyrp/game) that holds the name typed out, in any case and with any
# spacing between its words. Comments are not texts and are left alone (they keep the history), and so are the menu
# files: they are loaded from the pk3 and cannot read rp_version.h. No build needed.
#
# Before the real tree, the test runs itself on a small built-in tree of known cases, so it is known to still catch
# a typed name and to pass the rest.
#
# python3 tests/textlint/t_modname.py [--root <tree>]
import os, re, sys, subprocess, tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SELFTEST = True
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == '--root' and args:
        ROOT, SELFTEST = args.pop(0), False
    else:
        print('usage: t_modname.py [--root <tree>]')
        sys.exit(2)

SRC_DIRS = ['codemp/game', 'codemp/cgame', 'codemp/ui', 'galaxyrp/game']
VERSION = 'galaxyrp/game/rp_version.h'          # where the name is defined: its own literal is the definition

bad = 0
def fail(msg):
    global bad
    print('FAIL ' + msg)
    bad += 1

def read(path):
    return open(path, encoding='latin-1').read()

m = re.search(r'#define\s+RP_MOD_NAME\s+"([^"]+)"', read(os.path.join(ROOT, VERSION)))
if not m:
    fail('RP_MOD_NAME not found in ' + VERSION)
    print('%d FAILED' % bad)
    sys.exit(1)
NAME = m.group(1)
# the name's words, any amount of space between them (but at least some where the name has it), any case
NAME_RE = re.compile(r'\s+'.join(re.escape(w) for w in NAME.split()), re.I)

def literals(src):
    """(line, text) of each string literal in C source, comments and character literals skipped."""
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            i += 1
        elif src.startswith('//', i):
            while i < n and src[i] != '\n':
                if src[i] == '\\' and i + 1 < n and src[i + 1] == '\n':   # a // comment continued by a backslash
                    line += 1
                    i += 1
                i += 1
        elif src.startswith('/*', i):
            end = src.find('*/', i + 2)
            end = n if end < 0 else end + 2
            line += src.count('\n', i, end)
            i = end
        elif c == '"' or c == "'":
            start_line, j, text = line, i + 1, []
            while j < n and src[j] != c:
                if src[j] == '\\' and j + 1 < n:
                    if src[j + 1] == '\n':
                        line += 1
                    text.append(src[j:j + 2])
                    j += 2
                    continue
                if src[j] == '\n':          # an unterminated literal: stop at the line's end
                    break
                text.append(src[j])
                j += 1
            if c == '"':
                yield start_line, ''.join(text)
            i = j + 1
        else:
            i += 1

checked = 0
for d in SRC_DIRS:
    base = os.path.join(ROOT, d)
    if not os.path.isdir(base):
        continue
    for dirpath, _, files in os.walk(base):
        for f in sorted(files):
            if not f.endswith(('.c', '.h', '.cpp')):
                continue
            path = os.path.join(dirpath, f)
            rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
            if rel == VERSION:
                continue
            for line, text in literals(read(path)):
                checked += 1
                if NAME_RE.search(text):
                    fail('%s:%d: "%s" types the mod name: write it as " RP_MOD_NAME "' % (rel, line, text[:120]))

def selftest():
    """Runs this test on a small tree; lines marked BAD must be the ones it flags."""
    src = r'''#include "g_local.h"
// Universe RP in a line comment
/* Universe RP in a block comment,
   "Universe RP" over two lines */
static const char *a = "Needs the Universe RP client.";	/*BAD*/
static const char *b = "Needs the " RP_MOD_NAME " client.";
static const char *c = "UNIVERSE   rp, any case and spacing";	/*BAD*/
static const char *d = "an \" escaped quote, then Universe RP";	/*BAD*/
static const char e = '"'; static const char *f = "after a quote character: Universe RP";	/*BAD*/
static const char *g = "//"; static const char *h = "not a comment: Universe RP";	/*BAD*/
static const char *k = "the folder UniverseRP is another name";
static const char *l = "Universe Quest";
'''
    with tempfile.TemporaryDirectory() as tmp:
        for d in ('codemp/game', 'galaxyrp/game'):
            os.makedirs(os.path.join(tmp, d))
        open(os.path.join(tmp, VERSION), 'w').write('#define RP_MOD_NAME\t\t"%s"\t\t// the name\n' % NAME)
        open(os.path.join(tmp, 'codemp/game/t.c'), 'w').write(src)
        want = {n for n, l in enumerate(src.split('\n'), 1) if '/*BAD*/' in l}
        r = subprocess.run([sys.executable, os.path.abspath(__file__), '--root', tmp], capture_output=True, text=True)
        got = {int(x) for x in re.findall(r'^FAIL codemp/game/t\.c:(\d+):', r.stdout, re.M)}
        other = [l for l in r.stdout.split('\n') if l.startswith('FAIL') and 'codemp/game/t.c:' not in l]
        if got != want or other or r.returncode != 1:
            fail('self-test: flagged lines %s, expected %s%s' % (sorted(got), sorted(want), ('; ' + '; '.join(other)) if other else ''))
        return len(want)

if SELFTEST:
    cases = selftest()
    print('%d FAILED' % bad if bad else 't_modname: ALL OK (%d literals checked, self-test %d cases)' % (checked, cases))
else:
    print('%d FAILED' % bad if bad else 't_modname: ALL OK (%d literals checked)' % checked)
sys.exit(1 if bad else 0)
