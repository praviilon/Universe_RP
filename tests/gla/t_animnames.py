#!/usr/bin/env python3
# GLX_U: [RP Anims] TEST ONLY: /anim's named animations (animations[] in codemp/game/bg_rpemotes.h). No build needed.
#
#   python3 tests/gla/t_animnames.py [--root <tree>]
#
# - The list: as many entries as MAX_WORDED_EMOTES (C fills a short list with NULL names, which /anim's loop compares
#   against); names in lower case letters and digits, not all digits (/anim would take them as a number), at most 33
#   characters (/anim list's column), unique ignoring case (/anim matches with Q_stricmp), in alphabetical order.
# - Every animation is one of anims.h's, with frames in the humanoid animation.cfg.
# - Every category is one of anim_headers[], and every heading has a name.
# - /anim list's pages (anim_list_pages[]): each category on exactly one page, and no page over PAGE_BUDGET server
#   commands -- a row each, 3 per heading, the closing line and the hint; a client is dropped past 128 unacknowledged
#   ones.
#
# Before the real tree, the test runs itself on small built-in trees of known cases, so it is known to still catch
# each of these.
import os, re, sys, tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SELFTEST = True
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == '--root' and args:
        ROOT, SELFTEST = args.pop(0), False
    else:
        print('usage: t_animnames.py [--root <tree>]')
        sys.exit(2)

LIST = 'codemp/game/bg_rpemotes.h'
ANIMS = 'codemp/game/anims.h'
CFG = 'assets/client/models/players/_humanoid/animation.cfg'
PAGE_BUDGET = 100
NAME_WIDTH = 33		# get_max_spaces_right() in g_cmds.c


def nocomments(text):
    return re.sub(r'/\*.*?\*/', '', re.sub(r'//[^\n]*', '', text), flags=re.S)


def define(text, name):
    m = re.search(r'#define\s+%s\s+(\d+)' % name, text)
    return int(m.group(1)) if m else None


def check_tree(root):
    """the problems found in a tree, as texts, and what was counted"""
    out = []
    text = nocomments(open(os.path.join(root, LIST)).read())
    size, ncat, npages = define(text, 'MAX_WORDED_EMOTES'), define(text, 'MAX_EMOTE_CATEGORIES'), define(text, 'ANIM_LIST_PAGES')
    m = re.search(r'animations\[MAX_WORDED_EMOTES\][^{]*\{(.*?)\n\};', text, re.S)
    ents = re.findall(r'\{\s*"([^"]*)"\s*,\s*(\w+)\s*,\s*"([^"]*)"\s*\}', m.group(1)) if m else []
    m = re.search(r'anim_headers\[MAX_EMOTE_CATEGORIES\]\[\d+\][^{]*\{(.*?)\};', text, re.S)
    headers = re.findall(r'"([^"]*)"', m.group(1)) if m else []
    m = re.search(r'anim_list_pages\[ANIM_LIST_PAGES \+ 1\][^{]*\{(.*?)\};', text, re.S)
    pages = [ncat if p.strip() == 'MAX_EMOTE_CATEGORIES' else int(p) for p in m.group(1).split(',')] if m else []

    if len(ents) != size:
        out.append('%d names, MAX_WORDED_EMOTES is %s' % (len(ents), size))
    if len(headers) != ncat:
        out.append('%d headings, MAX_EMOTE_CATEGORIES is %s' % (len(headers), ncat))
    enum_text = nocomments(open(os.path.join(root, ANIMS)).read())
    enum = set(re.findall(r'\b(\w+)\s*,', enum_text[enum_text.index('{') + 1:enum_text.index('MAX_ANIMATIONS')]))
    framed = set()
    for line in open(os.path.join(root, CFG), encoding='latin-1'):
        w = line.split('//')[0].split()
        if len(w) >= 3 and w[2].lstrip('-').isdigit() and int(w[2]) > 0:
            framed.add(w[0])
    names = [e[0] for e in ents]
    if names != sorted(names):
        at = next(i for i, (x, y) in enumerate(zip(names, sorted(names))) if x != y)
        out.append('not in alphabetical order at %s' % names[at])
    seen = set()
    for name, code, cat in ents:
        if not re.fullmatch(r'[a-z0-9]+', name) or name.isdigit() or len(name) > NAME_WIDTH:
            out.append('%s: not a name (lower case letters and digits, not all digits, at most %d)' % (name, NAME_WIDTH))
        if name.lower() in seen:
            out.append('%s twice' % name)
        seen.add(name.lower())
        if code not in enum:
            out.append('%s: %s is not in %s' % (name, code, ANIMS))
        elif code not in framed:
            out.append('%s: %s has no frames in %s' % (name, code, CFG))
        if cat not in headers:
            out.append('%s: category %s is not a heading' % (name, cat))
    rows = {h: sum(1 for e in ents if e[2] == h) for h in headers}
    for h in headers:
        if not rows[h]:
            out.append('heading %s has no names' % h)
    if len(pages) != (npages or 0) + 1 or pages[:1] != [0] or pages[-1:] != [ncat] or pages != sorted(set(pages)):
        out.append('anim_list_pages %s does not split the %s headings into %s pages' % (pages, ncat, npages))
    else:
        for p in range(npages):
            cats = headers[pages[p]:pages[p + 1]]
            cmds = sum(rows[h] for h in cats) + 3 * len(cats) + 2
            if cmds > PAGE_BUDGET:
                out.append('page %d (%s) is %d server commands, over %d' % (p + 1, ', '.join(cats), cmds, PAGE_BUDGET))
    return out, len(ents), len(headers), npages


def selftest():
    """small trees, each with one known problem, and one without"""
    fails = 0
    lst = ('#define MAX_WORDED_EMOTES 3\t// a comment\n'
           'static const worded_animation_t animations[MAX_WORDED_EMOTES] UNUSED_VAR = {\n'
           '\t{"aim",\t\t\tBOTH_A,\t\t"Body"\t},\n\t{"bow",\t\t\tBOTH_B,\t\t"Body"\t},\t// note\n\t{"sit",\t\t\tBOTH_C,\t\t"Sit"\t},\n};\n'
           '#define MAX_EMOTE_CATEGORIES 2\n'
           'static const char anim_headers[MAX_EMOTE_CATEGORIES][50] UNUSED_VAR = {\n\t"Body",\n\t"Sit"\n};\n'
           '#define ANIM_LIST_PAGES 2\n'
           'static const int anim_list_pages[ANIM_LIST_PAGES + 1] UNUSED_VAR = { 0, 1, MAX_EMOTE_CATEGORIES };\n')
    anims = 'typedef enum {\n\tBOTH_A,\t//# one\n\tBOTH_B,\n\tBOTH_C,\n\tBOTH_D,\n\n\t//# #eol\n\tMAX_ANIMATIONS,\n} animNumber_t;\n'
    cfg = '// header\nBOTH_A\t\t0\t2\t-1\t20\nBOTH_B\t\t2\t3\t0\t10\nBOTH_C\t\t5\t1\t-1\t10\n'
    many = ''.join('\t{"b%03d",\t\t\tBOTH_A,\t\t"Body"\t},\n' % i for i in range(96))
    cases = [
        ('clean', {}, None),
        ('size', {LIST: lst.replace('EMOTES 3', 'EMOTES 4')}, '3 names, MAX_WORDED_EMOTES is 4'),
        ('order', {LIST: lst.replace('"aim"', '"zap"')}, 'not in alphabetical order at zap'),
        ('twice', {LIST: lst.replace('"bow"', '"aim"')}, 'aim twice'),
        ('upper case', {LIST: lst.replace('"bow"', '"Bow"')}, 'Bow: not a name'),
        ('all digits', {LIST: lst.replace('"bow"', '"12"')}, '12: not a name'),
        ('unknown animation', {LIST: lst.replace('BOTH_B', 'BOTH_X')}, 'BOTH_X is not in'),
        ('no frames', {LIST: lst.replace('BOTH_B', 'BOTH_D')}, 'BOTH_D has no frames'),
        ('unknown category', {LIST: lst.replace('"Sit"\t}', '"Lie"\t}')}, 'category Lie is not a heading'),
        ('empty heading', {LIST: lst.replace('"Sit"\t}', '"Body"\t}')}, 'heading Sit has no names'),
        ('pages', {LIST: lst.replace('{ 0, 1, MAX', '{ 0, 2, MAX')}, 'does not split'),
        ('a page too big', {LIST: lst.replace('EMOTES 3', 'EMOTES 99').replace('\t{"bow"', many + '\t{"bow"')},
         'page 1 (Body) is 103 server commands'),
    ]
    for label, changes, expect in cases:
        with tempfile.TemporaryDirectory() as d:
            files = {LIST: lst, ANIMS: anims, CFG: cfg}
            files.update(changes)
            for rel, text in files.items():
                os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
                open(os.path.join(d, rel), 'w', newline='\n').write(text)
            probs = check_tree(d)[0]
            ok = (not probs) if expect is None else any(expect in p for p in probs)
            if not ok:
                fails += 1
                print('SELF-TEST FAIL (%s): %s' % (label, probs))
    return fails, len(cases)


fails, cases = selftest() if SELFTEST else (0, 0)
probs, names, headers, pages = check_tree(ROOT)
for p in probs:
    print('FAIL', p)
fails += len(probs)
if fails:
    print('t_animnames: %d FAILED' % fails)
    sys.exit(1)
print('t_animnames: ALL OK (%d names, %d categories, %d pages%s)' % (names, headers, pages,
      ', self-test %d cases' % cases if SELFTEST else ''))
