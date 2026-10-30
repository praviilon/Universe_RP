#!/usr/bin/env python3
# TEST ONLY: the /enthelp data (codemp/game/g_enthelp_data.h). Every string goes out inside a print command, so a
# double quote cuts the print short (the rest of the line is lost), and the file's rule keeps out ';', '^' and tabs
# too. Checks, for every entry: those characters; spawnflag lines "value|NAME|meaning" and key lines
# "key|default|meaning" as g_enthelp.c reads them, each fitting one print; a text; names unique and in order; and
# every "also" name being an entry or an item.
import os, re, sys

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
DATA = os.path.join(root, 'codemp', 'game', 'g_enthelp_data.h')
CODE = os.path.join(root, 'codemp', 'game', 'g_enthelp.c')
VERSION = os.path.join(root, 'galaxyrp', 'game', 'rp_version.h')
ITEMS = os.path.join(root, 'codemp', 'game', 'bg_misc.c')

bad = 0
def fail(msg):
    global bad
    print('FAIL ' + msg)
    bad += 1

def read(path):
    return open(path, encoding='latin-1').read()

m = re.search(r'#define\s+RP_EH_PRINT_MAX\s+(\d+)', read(CODE))
PRINT_MAX = int(m.group(1)) if m else 0
m = re.search(r'#define\s+RP_MOD_NAME\s+"([^"]*)"', read(VERSION))
MACROS = {'RP_MOD_NAME': m.group(1) if m else 'Universe RP'}
MAX_QPATH = 64   # g_enthelp.c: a key or flag value is read into a MAX_QPATH buffer, a default or NAME into 256

# the items /enthelp knows without an entry (bg_itemlist's classnames)
text = read(ITEMS)
start = text.find('bg_itemlist[]')
items = set(re.findall(r'\n\t\{\s*\n\s*"(\w+)",', text[start:]))
if start < 0 or not items:
    fail('no item classnames found in bg_misc.c')

# --- a small C tokenizer: string literals, names, numbers and punctuation, comments dropped
src = read(DATA)
start = src.find('rp_ent_help[] = {')
if start < 0:
    fail('rp_ent_help[] not found')
    sys.exit(1)
tok_re = re.compile(r'\s+|//[^\n]*|/\*.*?\*/|"((?:[^"\\\n]|\\.)*)"|([A-Za-z_]\w*)|(\d+)|(.)', re.S)
ESC = {'n': '\n', 't': '\t', '"': '"', '\\': '\\', "'": "'"}
def unescape(s):
    return re.sub(r'\\(.)', lambda e: ESC.get(e.group(1), '\\' + e.group(1)), s)

toks = []
pos = start + len('rp_ent_help[] = ')
for t in tok_re.finditer(src, pos):
    if t.group(1) is not None:
        toks.append(('s', unescape(t.group(1)), t.start()))
    elif t.group(2):
        toks.append(('n', t.group(2), t.start()))
    elif t.group(3):
        toks.append(('d', t.group(3), t.start()))
    elif t.group(4):
        toks.append(('p', t.group(4), t.start()))
        if t.group(4) == ';':
            break

def lineno(off):
    return src.count('\n', 0, off) + 1

# entries: { field, field, ... } inside the outer braces; a field is literals and macros side by side, NULL, or flags
entries = []
i = 1   # past the outer '{'
while i < len(toks) and not (toks[i][0] == 'p' and toks[i][1] == '}'):
    if toks[i][0] == 'p' and toks[i][1] == ',':
        i += 1
        continue
    if not (toks[i][0] == 'p' and toks[i][1] == '{'):
        fail('line %d: unexpected %r' % (lineno(toks[i][2]), toks[i][1]))
        break
    at = toks[i][2]
    i += 1
    fields, cur = [], []
    while not (toks[i][0] == 'p' and toks[i][1] == '}'):
        if toks[i][0] == 'p' and toks[i][1] == ',':
            fields.append(cur)
            cur = []
        else:
            cur.append(toks[i])
        i += 1
    fields.append(cur)
    i += 1
    entries.append((at, fields))

def as_string(field, where):
    if len(field) == 1 and field[0][1] == 'NULL':
        return None
    out = ''
    for kind, val, _ in field:
        if kind == 's':
            out += val
        elif kind == 'n' and val in MACROS:
            out += MACROS[val]
        else:
            fail('%s: %r in a string field' % (where, val))
    return out

NAMES = ('name', 'note', 'text', 'spawnflags', 'keys', 'example', 'also')
parsed = []
for at, fields in entries:
    where = 'line %d' % lineno(at)
    if len(fields) != 8:
        fail('%s: %d fields, not 8' % (where, len(fields)))
        continue
    e = {'line': lineno(at)}
    e['name'] = as_string(fields[0], where)
    e['note'] = as_string(fields[2], where)
    for k, f in zip(NAMES[2:], fields[3:]):
        e[k] = as_string(f, where)
    e['flags'] = ' '.join(t[1] for t in fields[1])
    parsed.append(e)

if len(parsed) < 200:
    fail('only %d entries parsed' % len(parsed))

FORBIDDEN = {'"': 'a double quote', ';': "a ';'", '^': "a '^'", '\t': 'a tab'}
names = [e['name'] or '' for e in parsed]
known = set(n.lower() for n in names) | set(n.lower() for n in items)

for e in parsed:
    where = '%s (line %d)' % (e['name'], e['line'])
    for k in NAMES:
        s = e[k]
        if s is None:
            continue
        for ch, what in FORBIDDEN.items():
            if ch in s:
                n = s.index(ch)
                fail('%s %s: %s in ...%s...' % (where, k, what, s[max(0, n - 30):n + 10].replace('\n', '\\n')))
    if not e['text']:
        fail('%s: no text' % where)
    if 'RP_EH_MAPONLY' in e['flags'] or 'RP_EH_GAMETYPE' in e['flags']:
        if not e['note']:
            fail('%s: no note for its flags' % where)
    # spawnflags: "value|NAME|meaning", a number and a name, one print each
    for ln in (e['spawnflags'] or '').split('\n') if e['spawnflags'] else []:
        parts = ln.split('|', 2)
        if len(parts) != 3 or not re.fullmatch(r'\d+', parts[0]) or not re.fullmatch(r'[A-Z0-9_]+', parts[1]) or not parts[2].strip():
            fail('%s spawnflag line %r: not value|NAME|meaning' % (where, ln[:60]))
            continue
        shown = '^3%s %s^7 - %s\n' % tuple(parts)
        if len(parts[2]) >= PRINT_MAX or len(shown) > PRINT_MAX:
            fail('%s spawnflag %s: %d characters, more than one print (%d)' % (where, parts[1], len(shown), PRINT_MAX))
    # keys: "key|default|meaning", one print each
    for ln in (e['keys'] or '').split('\n') if e['keys'] else []:
        parts = ln.split('|', 2)
        # (a topic may list words rather than keys, as zyk_weather's effects: only a name and a meaning are needed)
        if len(parts) != 3 or not parts[0].strip() or not parts[2].strip():
            fail('%s key line %r: not key|default|meaning' % (where, ln[:60]))
            continue
        if len(parts[0]) >= MAX_QPATH or len(parts[1]) >= 256:
            fail('%s key %s: key or default too long' % (where, parts[0]))
        shown = ('^3%s^7 (default %s) - %s\n' % tuple(parts)) if parts[1] else ('^3%s^7 - %s\n' % (parts[0], parts[2]))
        if len(parts[2]) >= PRINT_MAX or len(shown) > PRINT_MAX:
            fail('%s key %s: %d characters, more than one print (%d)' % (where, parts[0], len(shown), PRINT_MAX))
    for a in (e['also'] or '').split():
        if a.lower() not in known:
            fail('%s also: %s is neither an entry nor an item' % (where, a))

lower = [n.lower() for n in names]
for n in sorted(set(lower)):
    if lower.count(n) > 1:
        fail('%s: %d entries' % (n, lower.count(n)))
for a, b in zip(lower, lower[1:]):
    if a > b:
        fail('order: %s before %s' % (a, b))

print('%d FAILED' % bad if bad else 't_enthelplint: ALL OK (%d entries)' % len(parsed))
sys.exit(1 if bad else 0)
