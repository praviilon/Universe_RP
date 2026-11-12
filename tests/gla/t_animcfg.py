#!/usr/bin/env python3
# GLX_U: [RP Anims] TEST ONLY: the humanoid animation.cfg against the code and the .gla. No build or game files needed.
#
#   python3 tests/gla/t_animcfg.py [--root <tree>]
#
# - Its size: the game, cgame and ui (bg_panimate.c BG_ParseAnimationFile, ui_main.c UI_ParseAnimationFile) read
#   the file into a 60000-byte buffer and drop the game on one of 59999 bytes or more -- a vanilla client's code too,
#   so the limit can't be raised for them. Measured with Windows line ends (CRLF), as a Windows checkout packs it.
# - Every name is one of anims.h's (ROOT aside: the parser skips it), and no name is there twice.
# - Every line has its four numbers, a frame count over 0, and frames inside the .gla.
# - animtable.h lists anims.h's names in the same order, so each name's number is its place in the enum.
#
# Before the real tree, the test runs itself on small built-in trees of known cases, so it is known to still catch
# each of these.
import os, re, struct, sys, tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SELFTEST = True
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == '--root' and args:
        ROOT, SELFTEST = args.pop(0), False
    else:
        print('usage: t_animcfg.py [--root <tree>]')
        sys.exit(2)

CFG = 'assets/client/models/players/_humanoid/animation.cfg'
GLA = 'assets/client/models/players/_humanoid/_humanoid.gla'
ANIMS = 'codemp/game/anims.h'
TABLE = 'codemp/cgame/animtable.h'
BUFFER = 60000		# BGPAFtext / UIPAFtext; a file of BUFFER - 1 bytes or more is refused


def check_tree(root):
    """the problems found in a tree, as texts"""
    out = []
    raw = open(os.path.join(root, CFG), 'rb').read()
    lf = raw.replace(b'\r\n', b'\n')
    crlf = len(lf) + lf.count(b'\n')
    if crlf >= BUFFER - 1:
        out.append('%s is %d bytes with CRLF line ends: the game reads at most %d' % (CFG, crlf, BUFFER - 2))
    enum_text = re.sub(r'//[^\n]*', '', open(os.path.join(root, ANIMS)).read())
    body = enum_text[enum_text.index('{') + 1:enum_text.index('MAX_ANIMATIONS')]
    enum = re.findall(r'\b(\w+)\s*,', body)
    table_text = re.sub(r'//[^\n]*', '', open(os.path.join(root, TABLE)).read())
    table = re.findall(r'ENUM2STRING\(\s*(\w+)\s*\)', table_text)
    if table != enum:
        at = next((i for i, (x, y) in enumerate(zip(enum, table)) if x != y), min(len(enum), len(table)))
        out.append('%s and %s differ at %d (%s / %s; %d and %d names)' % (ANIMS, TABLE, at,
                   enum[at] if at < len(enum) else '-', table[at] if at < len(table) else '-', len(enum), len(table)))
    gla = open(os.path.join(root, GLA), 'rb').read(100)
    frames = struct.unpack_from('<i', gla, 76)[0]
    known, seen = set(enum), set()
    for n, line in enumerate(lf.decode('latin-1').split('\n'), 1):
        words = line.split('//')[0].split()
        if not words:
            continue
        name = words[0]
        if name != 'ROOT' and name not in known:
            out.append('line %d: %s is not in %s' % (n, name, ANIMS))
        if name in seen:
            out.append('line %d: %s twice' % (n, name))
        seen.add(name)
        try:
            first, count, loop, fps = int(words[1]), int(words[2]), int(words[3]), float(words[4])
        except (IndexError, ValueError):
            out.append('line %d: %s: not four numbers' % (n, name))
            continue
        if count <= 0 or first < 0 or first + count > frames:
            out.append('line %d: %s: frames %d-%d, the .gla has %d' % (n, name, first, first + count - 1, frames))
    return out, len(seen), crlf


def selftest():
    """small trees, each with one known problem, and one without"""
    fails = 0
    anims = 'typedef enum {\n\tBOTH_A,\t//# one\n\tBOTH_B,\n\n\t//# #eol\n\tMAX_ANIMATIONS,\n} animNumber_t;\n'
    table = 'stringID_table_t animTable [MAX_ANIMATIONS+1] =\n{\n\tENUM2STRING(BOTH_A),\n\tENUM2STRING( BOTH_B ),\n\t{NULL,-1}\n};\n'
    cfg = '// header\nBOTH_A\t\t\t\t0\t2\t-1\t20\nBOTH_B\t\t\t\t2\t3\t0\t10\nROOT\t\t\t\t0\t1\t-1\t1\n'
    gla = b'2LGA' + struct.pack('<i', 6) + b'\0' * 64 + struct.pack('<fi', 1.0, 5) + b'\0' * 20
    cases = [
        ('clean', {}, None),
        ('too big', {CFG: cfg + '// ' + 'x' * 59990 + '\n'}, 'CRLF line ends'),
        ('unknown name', {CFG: cfg + 'BOTH_C\t0\t1\t-1\t1\n'}, 'BOTH_C is not in'),
        ('twice', {CFG: cfg + 'BOTH_A\t0\t1\t-1\t1\n'}, 'BOTH_A twice'),
        ('past the .gla', {CFG: cfg.replace('2\t3\t0', '2\t4\t0')}, 'frames 2-5, the .gla has 5'),
        ('a number missing', {CFG: cfg.replace('\t10\n', '\n')}, 'not four numbers'),
        ('table order', {TABLE: table.replace('BOTH_A),\n\tENUM2STRING( BOTH_B )', 'BOTH_B),\n\tENUM2STRING( BOTH_A )')},
         'differ at 0'),
        ('table short', {TABLE: table.replace('\tENUM2STRING( BOTH_B ),\n', '')}, 'differ at 1'),
    ]
    for label, changes, expect in cases:
        with tempfile.TemporaryDirectory() as d:
            files = {ANIMS: anims, TABLE: table, CFG: cfg}
            files.update(changes)
            for rel, text in files.items():
                os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
                open(os.path.join(d, rel), 'w', newline='\n').write(text)
            open(os.path.join(d, GLA), 'wb').write(gla)
            probs = check_tree(d)[0]
            ok = (not probs) if expect is None else any(expect in p for p in probs)
            if not ok:
                fails += 1
                print('SELF-TEST FAIL (%s): %s' % (label, probs))
    return fails, len(cases)


fails, cases = selftest() if SELFTEST else (0, 0)
probs, names, size = check_tree(ROOT)
for p in probs:
    print('FAIL', p)
fails += len(probs)
if fails:
    print('t_animcfg: %d FAILED' % fails)
    sys.exit(1)
print('t_animcfg: ALL OK (%d names, %d of %d bytes with CRLF%s)' % (names, size, BUFFER - 2,
      ', self-test %d cases' % cases if SELFTEST else ''))
