#!/usr/bin/env python3
# TEST ONLY: the texts the server sends players (codemp/game, galaxyrp/game). No build needed, about 30 s.
#
# A text reaches the player's game as a server command -- print "...", cp "...", chat "...", tchat, lchat, ltchat,
# rpdlg <id> <s> "title" "text" "option"... -- which the game splits with the engine's tokenizer (Cmd_TokenizeString2):
#   - a double quote inside a text ends the argument: print, cp, chat and tchat show argument 1 only, the rest is lost;
#   - a %s left outside the quotes is split at its first space;
#   - "@@@WORD" in a print or cp is swapped for a StringEd text (CG_CheckSVStringEdRef);
#   - a command over 1022 characters is dropped by the engine (SV_SendServerCommand);
#   - a chat or tchat text over 599 characters is cut (MAX_SAY_TEXT in CG_Chat_f);
#   - a configstring ("cs n \"...\"") is rebuilt by the client from the pieces: its quotes are lost.
# A Lua script's string can hold anything, so one that reaches a player text must have its double quotes replaced on
# the way: a loop doing it in that buffer (buf[i] == '"'), Q_strstrip with '"' in its set, or a function that does
# it to its first parameter (RP_LuaApi_Clean).
#
# Every trap->SendServerCommand and trap->SetConfigstring is followed back to its literals: va(fmt, ...) and its %s
# values, a ternary, a parameter (that function then counts as a sender and its callers are checked), a printf-like
# wrapper (Q_vsnprintf of a format parameter), a local or global buffer written by Q_strncpyz, Com_sprintf, Q_strcat,
# strcpy or "=", a function's return values, a static array of strings. Values it can't follow (player names, command
# arguments, entity keys, database fields) cannot hold a double quote; --dynamic <file> lists them.
#
# ALLOW: a case the analysis can't see through, checked by hand. An entry names the file, the function, text from
# the flagged source line and the reason; one that no longer matches anything fails ("stale"), so a changed line is
# looked at again. Before the real tree, the test runs itself on a small built-in tree of known cases, so it is
# known to still catch each rule.
#
# python3 tests/textlint/t_textlint.py [-v] [--dynamic <file>] [--root <tree>]
#   -v: also the functions and buffers found to carry a sent text, and the console commands built with a %s
#       outside quotes (a ';' or a line break in that value would run another command; information only).
import os, re, sys, collections, subprocess, tempfile

try:
    sys.stdout.reconfigure(errors='replace')      # a source line printed on a console that can't show every character
except AttributeError:
    pass

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
VERBOSE, DYNAMIC, SELFTEST = False, None, True
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == '-v':
        VERBOSE = True
    elif a == '--dynamic' and args:
        DYNAMIC = args.pop(0)
    elif a == '--root' and args:
        ROOT, SELFTEST = args.pop(0), False
    else:
        print('usage: t_textlint.py [-v] [--dynamic <file>] [--root <tree>]')
        sys.exit(2)

SRC_DIRS = ['codemp/game', 'galaxyrp/game']
HDR_DIRS = ['codemp/game', 'galaxyrp/game', 'shared/qcommon', 'codemp/qcommon']
SKIP_FILES = {'g_enthelp_data.h'}            # tests/enthelp has its own lint
CMD_MAX = 1022                                 # SV_SendServerCommand drops anything longer
SAY_MAX = 600                                  # MAX_SAY_TEXT: CG_Chat_f's buffer for chat and tchat
TEXT_ARGS = {'print': [1], 'cp': [1], 'chat': [1], 'tchat': [1], 'lchat': [1, 2, 3, 4], 'ltchat': [1, 2, 3, 4],
             'rpdlg': list(range(3, 11))}     # the arguments each shows as text (rpdlg: cg_rpdialog.c)
STRINGED = {'print', 'cp'}                     # CG_CheckSVStringEdRef runs on these
LUA_STRINGS = r'luaL_checkstring|luaL_optstring|luaL_checklstring|lua_tostring|lua_tolstring|luaL_tolstring'

# (file, function, text in the flagged source line, why it is not a problem)
ALLOW = [
    ('codemp/game/g_cmds.c', 'G_VoteMap',
     'Com_sprintf( level.voteString, sizeof( level.voteString ), "%s %s; set nextmap \\"%s\\"", arg1, arg2, s );',
     'voteString is the console command the vote runs (its quotes are wanted); the display string is set apart and '
     'voteStringClean has its quotes stripped. The analysis sees voteString copied to voteDisplayString in other votes.'),
    ('codemp/game/g_cmds.c', 'Cmd_CallVote_f',
     'Com_sprintf( level.voteString, sizeof( level.voteString ), "%s \\"%s\\"", arg1, arg2 );',
     'the "otherwise assume it\'s a command" branch: every vote in validVoteStrings has a handler, so it is never reached'),
    ('galaxyrp/game/rp_lua_world.c', 'RP_LuaWorld_Prop', 'const char *classname = luaL_checkstring( L, 1 );',
     'refused unless it is a class a script may spawn (RP_MapExemptClass): no double quote'),
    ('galaxyrp/game/rp_lua_world.c', 'RP_LuaWorld_Npc', 'const char *type = luaL_checkstring( L, 1 );',
     'refused unless the server has loaded that NPC type (RP_NpcTypeKnown); the names come from parsed .npc files'),
    ('galaxyrp/game/rp_lua_world.c', 'RP_LuaWorld_Vehicle', 'const char *type = luaL_checkstring( L, 1 );',
     'refused unless the server has loaded that vehicle type (RP_NpcTypeKnown, RP_NpcTypeIsVehicle)'),
]

# ------------------------------------------------------------------ C text helpers
def strip_comments(s):
    """comments become spaces (line breaks kept, so offsets and line numbers hold); literals are kept"""
    out, i, n = [], 0, len(s)
    while i < n:
        c = s[i]
        if c == '/' and i + 1 < n and s[i + 1] == '/':
            j = s.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j
        elif c == '/' and i + 1 < n and s[i + 1] == '*':
            j = s.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', s[i:j])); i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and s[j] != c:
                if s[j] == '\\':
                    j += 1
                elif s[j] == '\n':
                    break
                j += 1
            out.append(s[i:j + 1]); i = j + 1
        else:
            out.append(c); i += 1
    return ''.join(out)

def skip_literal(s, i):
    q, j = s[i], i + 1
    while j < len(s) and s[j] != q:
        if s[j] == '\\':
            j += 1
        j += 1
    return j + 1

def match_paren(s, i):
    """s[i] opens a bracket: the index just past its match"""
    depth, j = 0, i
    while j < len(s):
        c = s[j]
        if c in '"\'':
            j = skip_literal(s, j); continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                return j + 1
        j += 1
    return len(s)

def split_top(s, sep=','):
    parts, depth, cur, j = [], 0, 0, 0
    while j < len(s):
        c = s[j]
        if c in '"\'':
            j = skip_literal(s, j); continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(s[cur:j]); cur = j + 1
        j += 1
    parts.append(s[cur:])
    return [p.strip() for p in parts]

def find_top(s, ch):
    depth, j = 0, 0
    while j < len(s):
        c = s[j]
        if c in '"\'':
            j = skip_literal(s, j); continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ch and depth == 0:
            return j
        j += 1
    return -1

ESC = {'n': '\n', 't': '\t', 'r': '\r', '"': '"', '\\': '\\', "'": "'", 'a': '\a', '0': '\0', '?': '?'}
def unescape(body):
    def rep(m):
        e = m.group(1)
        if e[0] == 'x':
            return chr(int(e[1:], 16))
        if e[0] in '01234567' and len(e) > 1:
            return chr(int(e, 8))
        return ESC.get(e, e)
    return re.sub(r'\\(x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|.)', rep, body)

LIT_RE = re.compile(r'\s*"((?:[^"\\\n]|\\.)*)"', re.S)
ID_RE = re.compile(r'\s*([A-Za-z_]\w*)')

# ------------------------------------------------------------------ load
files, files_raw = {}, {}
for d in SRC_DIRS:
    for f in sorted(os.listdir(os.path.join(ROOT, d))):
        if f.endswith('.c') and f not in SKIP_FILES:
            p = d + '/' + f
            files_raw[p] = open(os.path.join(ROOT, d, f), encoding='latin-1').read().replace('\r\n', '\n')
            files[p] = strip_comments(files_raw[p])

macros = {}
for d in HDR_DIRS:
    if not os.path.isdir(os.path.join(ROOT, d)):
        continue
    for f in sorted(os.listdir(os.path.join(ROOT, d))):
        if (f.endswith('.h') or f.endswith('.c')) and f not in SKIP_FILES:
            txt = strip_comments(open(os.path.join(ROOT, d, f), encoding='latin-1').read().replace('\r\n', '\n'))
            for m in re.finditer(r'^[ \t]*#[ \t]*define[ \t]+([A-Za-z_]\w*)[ \t]+((?:"(?:[^"\\\n]|\\.)*"|[A-Za-z_]\w*|[ \t])+)$', txt, re.M):
                macros.setdefault(m.group(1), m.group(2).strip())

def literal_value(expr, seen=frozenset()):
    """string literals and macros naming them, side by side -> the string; anything else -> None"""
    s, out, i = expr.strip(), '', 0
    if not s:
        return None
    while i < len(s):
        m = LIT_RE.match(s, i)
        if m:
            out += unescape(m.group(1)); i = m.end(); continue
        m = ID_RE.match(s, i)
        if m and m.group(1) in macros and m.group(1) not in seen:
            v = literal_value(macros[m.group(1)], seen | {m.group(1)})
            if v is None:
                return None
            out += v; i = m.end(); continue
        if s[i:].strip() == '':
            break
        return None
    return out

# ------------------------------------------------------------------ functions
Func = collections.namedtuple('Func', 'name file start body bodystart params')
funcs, by_name = [], collections.defaultdict(list)
for path, code in files.items():
    depth, i, n = 0, 0, len(code)
    while i < n:
        c = code[i]
        if c in '"\'':
            i = skip_literal(code, i); continue
        if c == '{' and depth == 0:
            j = i - 1
            while j >= 0 and code[j] in ' \t\r\n':
                j -= 1
            if j >= 0 and code[j] == ')':           # a function body: its header is name( params )
                d2, k = 0, j
                while k >= 0:
                    if code[k] == ')':
                        d2 += 1
                    elif code[k] == '(':
                        d2 -= 1
                        if d2 == 0:
                            break
                    k -= 1
                m = re.search(r'([A-Za-z_]\w*)\s*$', code[max(0, k - 200):k])
                e = match_paren(code, i)
                if m and m.group(1) not in ('if', 'while', 'for', 'switch', 'sizeof', 'return'):
                    params = []
                    for p in split_top(code[k + 1:j]):
                        if p and p not in ('void', '...'):
                            pm = re.search(r'([A-Za-z_]\w*)\s*(\[[^\]]*\]\s*)*$', p)
                            params.append(pm.group(1) if pm else '')
                    f = Func(m.group(1), path, k, code[i:e], i, params)
                    funcs.append(f); by_name[f.name].append(f)
                i = e
                continue
        if c in '{':
            depth += 1
        elif c == '}':
            depth -= 1
        i += 1

def lineof(path, off):
    return files[path].count('\n', 0, off) + 1

def func_at(path, off):
    for f in funcs:
        if f.file == path and f.bodystart <= off < f.bodystart + len(f.body):
            return f.name
    return ''

# file-scope char buffers and pointers (and members of structs declared in a .c file)
globals_ = set()
for path, code in files.items():
    spans = sorted((f.start, f.bodystart + len(f.body)) for f in funcs if f.file == path)
    out, last = [], 0
    for a0, b0 in spans:
        out.append(code[last:a0]); last = b0
    out.append(code[last:])
    for m in re.finditer(r'\bchar\s*(?:\*\s*)?([A-Za-z_]\w*)\s*(?:\[|=|;)', ''.join(out)):
        globals_.add(m.group(1))

# cleaner functions: the double quotes of their first parameter are replaced in the body (p[i] == '"')
cleaners = set()
for f in funcs:
    if f.params and re.search(r'(?<![\w.>])' + re.escape(f.params[0]) + r'\s*\[[^\]]+\]\s*==\s*\'"\'', f.body):
        cleaners.add(f.name)

# buffers whose double quotes are stripped before they are sent: Q_strstrip( buf, "...\"..." ) anywhere
stripped = set()
for path, code in files.items():
    for m in re.finditer(r'\bQ_strstrip\s*\(\s*([^,]+?)\s*,\s*"((?:[^"\\]|\\.)*)"', code):
        if '\\"' in m.group(2):
            stripped.add(re.split(r'\.|->', m.group(1))[-1].strip())

# static arrays of strings: name -> their literals
str_arrays = {}
for path, code in files.items():
    for m in re.finditer(r'\b(?:static\s+)?(?:const\s+)?char\s*\*\s*(?:const\s+)?([A-Za-z_]\w*)\s*\[[^\]]*\]\s*=\s*\{', code):
        e = match_paren(code, m.end() - 1)
        vals = [literal_value(x) for x in split_top(code[m.end():e - 1]) if x and x != 'NULL']
        if vals and all(v is not None for v in vals):
            str_arrays[m.group(1)] = vals

# ------------------------------------------------------------------ the engine's tokenizer (Cmd_TokenizeString2, cmd.cpp)
def tokenize(text):
    argv, i, n = [], 0, len(text)
    while True:
        if len(argv) == 1024:
            return argv
        while True:
            while i < n and ord(text[i]) <= 32:
                i += 1
            if i >= n:
                return argv
            if text.startswith('//', i):
                return argv
            if text.startswith('/*', i):
                j = text.find('*/', i)
                if j < 0:
                    return argv
                i = j + 2
            else:
                break
        if text[i] == '"':
            i += 1
            j = text.find('"', i)
            if j < 0:
                argv.append(text[i:]); return argv
            argv.append(text[i:j]); i = j + 1
            continue
        st = i
        while i < n and ord(text[i]) > 32:
            if text[i] == '"' or text.startswith('//', i) or text.startswith('/*', i):
                break
            i += 1
        argv.append(text[st:i])
        if i >= n:
            return argv

# ------------------------------------------------------------------ formats: each conversion becomes a marker character
FMT_RE = re.compile(r'%([-+ #0]*)(\*|\d+)?(?:\.(\*|\d+))?(hh|h|ll|l|L|q|j|z|t|I64)?([diouxXeEfgGcspn%])')
MARK_RE = re.compile('[\uE000-\uEFFF]')
def skeleton(fmt, args, is_fmt):
    """-> (fmt with each conversion replaced by a marker, {marker: (conversion, its argument)})"""
    if not is_fmt:
        return fmt, {}
    out, marks, ai, last = [], {}, 0, 0
    for m in FMT_RE.finditer(fmt):
        out.append(fmt[last:m.start()]); last = m.end()
        conv = m.group(5)
        if conv == '%':
            out.append('%'); continue
        ai += (m.group(2) == '*') + (m.group(3) == '*')
        mk = chr(0xE000 + len(marks))
        marks[mk] = (conv, args[ai] if ai < len(args) else None)
        ai += 1
        out.append(mk)
    out.append(fmt[last:])
    return ''.join(out), marks

def literal_parts(text):
    return MARK_RE.sub('\x00', text)

def show(s):
    return MARK_RE.sub('%s', s).replace('\n', '\\n')

# ------------------------------------------------------------------ analysis
# kinds: a whole command; a print/cp text; another text argument; a configstring; NOQ: its quotes are replaced
RAW, PTEXT, TEXT, CSTR = 'command', 'print text', 'text', 'configstring'
NOQ = ' (quotes replaced)'
sinks = collections.defaultdict(dict)   # function -> {parameter index: {(kind, is_format)}}
buffer_sinks = {}                       # buffer or field name -> {kinds}
return_sinks = {}                       # function -> {kinds}
findings, allowed, dynamic, console = (collections.OrderedDict() for _ in range(4))
stats = collections.Counter()
changed = True

def find(fn, off, what, msg):
    ln = lineof(fn.file, off)
    src = files_raw[fn.file].split('\n')[ln - 1]
    for k, (ap, af, at, why) in enumerate(ALLOW):
        if ap == fn.file and at in src and func_at(fn.file, off) == af:
            allowed[k] = (fn.file, ln)
            return
    findings.setdefault((fn.file, ln, what), msg)

def dyn(fn, off, e, kind, note=''):
    dynamic.setdefault((fn.file, lineof(fn.file, off), e), (kind, note))

def add_sink(fname, idx, kind, is_fmt):
    global changed
    cur = sinks[fname].setdefault(idx, set())
    if (kind, is_fmt) not in cur:
        cur.add((kind, is_fmt)); changed = True

def add_set(table, name, kind):
    global changed
    if kind not in table.setdefault(name, set()):
        table[name].add(kind); changed = True

def noq(kind):
    return kind if kind.endswith(NOQ) else kind + NOQ

def strip_expr(e):
    e = e.strip()
    while True:
        m = re.match(r'^\(\s*(?:const\s+)?(?:unsigned\s+)?(?:char|void)\s*\*\s*\)\s*', e)
        if m:
            e = e[m.end():].strip(); continue
        if e.startswith('(') and match_paren(e, 0) == len(e):
            e = e[1:-1].strip(); continue
        return e

def check_format(fmt_expr, args, kind, fn, off, depth):
    f = literal_value(strip_expr(fmt_expr))
    if f is not None:
        check_text(f, args, True, kind, fn, off, depth)
    elif fmt_expr in fn.params:
        add_sink(fn.name, fn.params.index(fmt_expr), kind, True)
    else:
        dyn(fn, off, fmt_expr, kind)

def check_value(expr, kind, fn, off, depth=0):
    """expr is sent as kind"""
    if depth > 12 or expr is None:
        return
    e = strip_expr(expr)
    q = find_top(e, '?')
    if q > 0:
        rest = e[q + 1:]
        c = find_top(rest, ':')
        if c >= 0:
            check_value(rest[:c], kind, fn, off, depth + 1)
            check_value(rest[c + 1:], kind, fn, off, depth + 1)
            return
    lit = literal_value(e)
    if lit is not None:
        check_text(lit, [], False, kind, fn, off, depth)
        return
    m = re.match(r'^(va|G_NewString)\s*\(', e)
    if m and match_paren(e, m.end() - 1) == len(e):
        a = split_top(e[m.end():-1])
        if m.group(1) == 'va':
            check_format(a[0], a[1:], kind, fn, off, depth)
        else:
            check_value(a[0], kind, fn, off, depth + 1)
        return
    m = re.match(r'^(' + LUA_STRINGS + r')\s*\(', e)
    if m:                                   # a script's string: anything at all
        if not kind.endswith(NOQ):
            find(fn, off, 'lua', "a Lua script's string (%s) reaches a %s with its double quotes as they are" % (m.group(1), kind))
        return
    if re.fullmatch(r'[A-Za-z_]\w*', e) and e in fn.params:
        add_sink(fn.name, fn.params.index(e), kind, False)
        return
    m = re.fullmatch(r'([A-Za-z_]\w*)\s*\[.*\]', e, re.S)
    if m and m.group(1) in str_arrays:
        for v in str_arrays[m.group(1)]:
            check_text(v, [], False, kind, fn, off, depth)
        return
    m = re.match(r'^([A-Za-z_]\w*)\s*\(', e)
    if m and match_paren(e, m.end() - 1) == len(e) and m.group(1) in by_name:
        add_set(return_sinks, m.group(1), kind)
        return
    if re.fullmatch(r'[A-Za-z_]\w*', e) and trace_local(e, kind, fn, off, depth):
        return
    m = re.fullmatch(r'.*?(?:\.|->)\s*([A-Za-z_]\w*)(\s*\[[^\]]*\])?', e, re.S)
    if not m:
        m = re.fullmatch(r'([A-Za-z_]\w*)(\s*\[[^\]]*\])?', e)
        if m and m.group(1) not in globals_:
            m = None
    if m:
        add_set(buffer_sinks, m.group(1), kind)
        dyn(fn, off, e, kind, '(buffer %s)' % m.group(1))
        return
    dyn(fn, off, e, kind)

WRITERS = [('Q_strncpyz', 'copy', 1), ('strcpy', 'copy', 1), ('strncpy', 'copy', 1), ('Q_strcat', 'copy', 2),
           ('strcat', 'copy', 1), ('Com_sprintf', 'fmt', 2), ('snprintf', 'fmt', 2), ('Q_vsnprintf', 'vfmt', 2),
           ('vsnprintf', 'vfmt', 2), ('vsprintf', 'vfmt', 1)]
_writes = {}
def writes(target, body, word):
    """(how, args, argument index, offset) of every write into target (a regex) in body (the same every round)"""
    key = (target, id(body))
    if key not in _writes:
        _writes[key] = _writes_uncached(target, body, word) if word in body else []
    return _writes[key]

def _writes_uncached(target, body, word):
    out = []
    for func, how, at in WRITERS:
        if func not in body:
            continue
        for m in re.finditer(r'\b' + func + r'\s*\(\s*(?:\(\s*char\s*\*\s*\)\s*)?' + target + r'\s*,', body):
            st = body.find('(', m.start())
            out.append((how, split_top(body[st + 1:match_paren(body, st) - 1]), at, m.start()))
    for m in re.finditer(target + r'\s*=(?!=)\s*', body):
        out.append(('assign', [body[m.end():body.find(';', m.end())]], 0, m.start()))
    return out

def local_re(var):
    return r'(?<![\w.>])' + re.escape(var) + r'(?!\w)'

def field_re(name):
    # a field: owner.name / owner->name[i]; a file-scope name may also stand alone
    owners = r'(?:[A-Za-z_]\w*(?:\s*\[[^\]]*\])?\s*(?:\.|->)\s*)' + ('*' if name in globals_ else '+')
    return r'(?<![\w.>])' + owners + re.escape(name) + r'(?:\s*\[[^\]]*\])?(?!\w)'

_cleans = {}
def cleans(target, body):
    """this function replaces the double quotes in target (a regex): a loop, Q_strstrip, or a cleaner function"""
    key = (target, id(body))
    if key not in _cleans:
        _cleans[key] = _cleans_uncached(target, body)
    return _cleans[key]

def _cleans_uncached(target, body):
    if "'\"'" not in body and 'Q_strstrip' not in body and not any(c in body for c in cleaners):
        return False
    if re.search(target + r'\s*\[[^\]]+\]\s*==\s*\'"\'', body):
        return True
    if re.search(r'\bQ_strstrip\s*\(\s*' + target + r'\s*,\s*"(?:[^"\\]|\\.)*\\"', body):
        return True
    return any(re.search(r'\b' + c + r'\s*\(\s*' + target + r'\s*[,)]', body) for c in cleaners if c in body)

def apply_writes(ws, kind, fn, depth):
    for how, a, at, woff in ws:
        o = fn.bodystart + woff
        if at >= len(a):
            continue
        if how in ('copy', 'assign'):
            check_value(a[at], kind, fn, o, depth + 1)
        elif how == 'fmt':
            check_format(a[at], a[at + 1:], kind, fn, o, depth + 1)
        elif a[at] in fn.params:              # vfmt: a printf-like wrapper's format parameter
            add_sink(fn.name, fn.params.index(a[at]), kind, True)

def trace_local(var, kind, fn, off, depth):
    if not re.search(r'\b(?:char|qboolean|int)\b[^;{}()]*' + local_re(var), fn.body):
        return False                          # not declared here: a global
    if cleans(local_re(var), fn.body):
        kind = noq(kind)
    ws = writes(local_re(var), fn.body, var)
    apply_writes(ws, kind, fn, depth)
    if not ws:
        dyn(fn, off, var, kind, 'local with no write found')
    return True

def check_text(text, fargs, is_fmt, kind, fn, off, depth):
    stats['texts'] += 1
    sk, marks = skeleton(text, fargs, is_fmt)
    if kind == RAW:
        if len(literal_parts(sk)) > CMD_MAX:
            find(fn, off, 'long', 'the command is %d characters before its values; the engine drops anything over %d' % (len(sk), CMD_MAX))
        argv = tokenize(sk)
        if not argv or argv[0] not in TEXT_ARGS:
            return
        cmd, want = argv[0], TEXT_ARGS[argv[0]]
        if len(argv) > max(want) + 1:
            find(fn, off, 'quote', '"%s": the text splits into %d arguments, the game shows %s; it is cut at "%s" [%s]'
                 % (cmd, len(argv) - 1, 'the first' if len(want) == 1 else 'the first %d' % len(want), show(argv[max(want) + 1])[:30], show(sk)[:110]))
        for k in want:
            if k >= len(argv):
                continue
            lit = literal_parts(argv[k])
            if cmd in STRINGED and '@@@' in lit:
                find(fn, off, '@@@', '"%s" text has @@@ (taken as a StringEd reference) [%s]' % (cmd, show(sk)[:110]))
            if cmd in ('chat', 'tchat') and len(lit) >= SAY_MAX:
                find(fn, off, 'say', '"%s" text is %d characters; the game keeps %d' % (cmd, len(lit), SAY_MAX - 1))
            for mk in MARK_RE.findall(argv[k]):
                if marks[mk][0] == 's':
                    check_value(marks[mk][1], PTEXT if cmd in STRINGED else TEXT, fn, off, depth + 1)
        inq = False                           # a %s outside the quotes: split at its spaces
        for ch in sk:
            if ch == '"':
                inq = not inq
            elif ch in marks and marks[ch][0] == 's' and not inq:
                find(fn, off, 'unquoted', '"%s": a %%s outside the quotes, so its value is cut at its first space [%s]' % (cmd, show(sk)[:100]))
        return
    lit = literal_parts(sk)                   # a text inside a quoted argument, or a configstring
    if '"' in lit and not kind.endswith(NOQ):
        n = lit.index('"')
        if kind.startswith(CSTR):
            find(fn, off, 'quote', 'a configstring has a double quote: the client rebuilds it from the pieces, the quotes are lost [...%s...]' % show(sk[max(0, n - 40):n + 30]))
        else:
            find(fn, off, 'quote', 'a %s sent inside quotes has a double quote: cut there [...%s...]' % (kind, show(sk[max(0, n - 40):n + 30])))
    if kind.startswith(PTEXT) and '@@@' in lit and fn.name != 'G_GetStringEdString':   # its "@@@<key>" is the reference, by design
        find(fn, off, '@@@', 'a print text has @@@ (taken as a StringEd reference) [%s]' % show(sk)[:100])
    for conv, ae in marks.values():
        if conv == 's':
            check_value(ae, kind, fn, off, depth + 1)

SINK_CALL = re.compile(r'(trap->SendServerCommand|trap->SetConfigstring|trap->SendConsoleCommand|\b[A-Za-z_]\w*)\s*\(')
def scan(fn):
    for m in SINK_CALL.finditer(fn.body):
        name = m.group(1)
        if name == 'trap->SendServerCommand':
            spec = {1: {(RAW, False)}}
        elif name == 'trap->SetConfigstring':
            spec = {1: {(CSTR, False)}}
        elif name == 'trap->SendConsoleCommand' or name in sinks:
            spec = sinks.get(name)
        else:
            continue
        st = m.end() - 1
        a = split_top(fn.body[st + 1:match_paren(fn.body, st) - 1])
        off = fn.bodystart + m.start()
        if name == 'trap->SendConsoleCommand':
            if len(a) >= 2:
                check_console(a[1], fn, off)
            continue
        stats['calls'] += 1
        for idx, kinds in list(spec.items()):
            for kind, is_fmt in sorted(kinds):
                if idx >= len(a):
                    continue
                if is_fmt:
                    check_format(a[idx], a[idx + 1:], kind, fn, off, 0)
                else:
                    check_value(a[idx], kind, fn, off)

def check_console(expr, fn, off):
    e = strip_expr(expr)
    m = re.match(r'^va\s*\(', e)
    if not m:
        return
    a = split_top(e[m.end():-1])
    f = literal_value(strip_expr(a[0]))
    if f is None:
        return
    sk, marks = skeleton(f, a[1:], True)
    inq = False
    for ch in sk:
        if ch == '"':
            inq = not inq
        elif ch in marks and marks[ch][0] == 's' and not inq:
            console.setdefault((fn.file, lineof(fn.file, off)), (show(sk)[:80], marks[ch][1]))

# ------------------------------------------------------------------ until nothing new is found
rounds = 0
while changed and rounds < 40:
    changed = False
    rounds += 1
    for t in (findings, allowed, dynamic, console, stats):
        t.clear()
    for fn in funcs:
        scan(fn)
    for name, kind in [(n, k) for n, ks in list(buffer_sinks.items()) for k in sorted(ks)]:
        target = field_re(name)
        for fn in funcs:
            if name not in fn.body or name in fn.params:
                continue
            if name in globals_ and re.search(r'\b(?:char|int|qboolean|float)\b[^;{}()]*' + local_re(name), fn.body):
                continue                      # a local of the same name hides the global here
            k = noq(kind) if name in stripped or cleans(target, fn.body) else kind
            apply_writes(writes(target, fn.body, name), k, fn, 1)
    for fname, kind in [(n, k) for n, ks in list(return_sinks.items()) for k in sorted(ks)]:
        for fn in by_name.get(fname, []):
            for m in re.finditer(r'\breturn\s+', fn.body):
                check_value(fn.body[m.end():fn.body.find(';', m.end())], kind, fn, fn.bodystart + m.start(), 1)

# ------------------------------------------------------------------ report
bad = 0
def fail(msg):
    global bad
    print('FAIL ' + msg)
    bad += 1

if rounds >= 40:
    fail('the analysis did not settle in 40 rounds')
for (path, ln, what), msg in findings.items():
    fail('%s:%d: %s' % (path, ln, msg))
for k, (ap, af, at, why) in enumerate(ALLOW):
    if k not in allowed:
        fail('stale allow entry (%s, %s): its line is gone or changed, or no longer flagged; check it again and update ALLOW' % (ap, af))
if VERBOSE:
    for k, (path, ln) in sorted(allowed.items(), key=lambda x: x[1]):
        print('allowed %s:%d: %s' % (path, ln, ALLOW[k][3]))
    print('functions that send a text they are given: %d' % sum(1 for f in sinks if sinks[f]))
    for f in sorted(sinks):
        print('  %s: %s' % (f, ', '.join('argument %d (%s%s)' % (i + 1, k, ', a format' if fm else '') for i, v in sorted(sinks[f].items()) for (k, fm) in sorted(v))))
    print('buffers whose contents are sent: %s' % ', '.join(sorted(buffer_sinks)))
    print('console commands with a %%s outside quotes: %d' % len(console))
    for (path, ln), (sk, ae) in console.items():
        print('  %s:%d: %s  <- %s' % (path, ln, sk, ae))
    print('%d functions, %d sending calls, %d texts checked, %d values not followed, %d rounds'
          % (len(funcs), stats['calls'], stats['texts'], len(dynamic), rounds))
if DYNAMIC:
    with open(DYNAMIC, 'w') as fh:
        for (path, ln, e), (kind, note) in dynamic.items():
            fh.write('%s:%d: [%s] %s %s\n' % (path, ln, kind, e, note))

# ------------------------------------------------------------------ the self-test: a small tree of known cases
SELF = r'''
char g_note[256];
typedef struct { char refusal[256]; char help[96]; } lvl_t; lvl_t level;
void T_QuoteLit( void ) { trap->SendServerCommand( 0, "print \"bad \"quoted\" text\n\"" ); }	/*BAD quote*/
void T_Unquoted( const char *s ) { trap->SendServerCommand( 0, va( "print %s\n", s ) ); }	/*BAD unquoted*/
void T_AtAtAt( void ) { trap->SendServerCommand( 0, "print \"see @@@KEY here\n\"" ); }	/*BAD @@@*/
void T_Long( void ) { trap->SendServerCommand( 0, "print \"LONGTEXT\"" ); }	/*BAD long*/
void T_Say( void ) { trap->SendServerCommand( 0, "chat \"SAYTEXT\"" ); }	/*BAD say*/
static void T_Tell( int n, const char *msg ) { trap->SendServerCommand( n, va( "print \"%s\n\"", msg ) ); }
void T_TellBad( void ) { T_Tell( 1, "a \"quoted\" word" ); }	/*BAD quote*/
void T_TellOk( void ) { T_Tell( 1, "no quotes, it's fine" ); }
static void T_Printf( int n, const char *fmt, ... ) { va_list a; char buf[512]; va_start( a, fmt ); Q_vsnprintf( buf, sizeof( buf ), fmt, a ); va_end( a ); trap->SendServerCommand( n, va( "print \"%s\"", buf ) ); }
void T_PrintfBad( const char *m ) { T_Printf( 1, "map \"%s\" missing\n", m ); }	/*BAD quote*/
static void T_Refuse( const char *why ) { Q_strncpyz( level.refusal, why, sizeof( level.refusal ) ); }
void T_RefuseBad( const char *m ) { T_Refuse( va( "\"%s\" refused", m ) ); }	/*BAD quote*/
void T_ShowRefusal( void ) { trap->SendServerCommand( 0, va( "print \"Refused: %s\n\"", level.refusal ) ); }
void T_Cs( void ) { trap->SetConfigstring( 5, "cs \"text\"" ); }	/*BAD quote*/
static const char *T_Name( int i ) { return i ? "plain" : "with \"quote\""; }	/*BAD quote*/
void T_Ret( void ) { trap->SendServerCommand( 0, va( "print \"%s\n\"", T_Name( 1 ) ) ); }
void T_Strip( const char *m ) { Com_sprintf( g_note, sizeof( g_note ), "x \"%s\"", m ); Q_strstrip( g_note, "\"\n", NULL ); }
void T_ShowNote( void ) { trap->SendServerCommand( 0, va( "print \"%s\n\"", g_note ) ); }
void T_Color( const char *n ) { trap->SendServerCommand( 0, va( "print \"" S_COLOR_RED "%s joined\n\"", n ) ); }
void T_Local( int k ) { const char *t; if ( k ) t = "cp \"one\n\""; else t = "cp \"two \"x\"\n\""; trap->SendServerCommand( 0, t ); }	/*BAD quote*/
static void T_Clean( char *s ) { int i; for ( i = 0; s[i]; i++ ) if ( s[i] == '"' ) s[i] = '\''; }
void T_LuaBad( lua_State *L ) { const char *s = luaL_checkstring( L, 1 ); trap->SendServerCommand( 0, va( "print \"%s\n\"", s ) ); }	/*BAD lua*/
void T_LuaLoop( lua_State *L ) { char b[64]; int i; Q_strncpyz( b, luaL_checkstring( L, 1 ), sizeof( b ) ); for ( i = 0; b[i]; i++ ) if ( b[i] == '"' ) b[i] = '\''; trap->SendServerCommand( 0, va( "print \"%s\n\"", b ) ); }
void T_LuaHelp( lua_State *L ) { const char *h = luaL_optstring( L, 2, "" ); Q_strncpyz( level.help, h, sizeof( level.help ) ); T_Clean( level.help ); }
void T_LuaHelpBad( lua_State *L ) { const char *h = luaL_optstring( L, 2, "" ); Q_strncpyz( level.help, h, sizeof( level.help ) ); }	/*BAD lua*/
void T_ShowHelp( void ) { trap->SendServerCommand( 0, va( "print \"%s\n\"", level.help ) ); }
'''
def selftest():
    with tempfile.TemporaryDirectory() as tmp:
        for d in SRC_DIRS + HDR_DIRS:
            os.makedirs(os.path.join(tmp, d), exist_ok=True)
        open(os.path.join(tmp, 'shared/qcommon/q_color.h'), 'w').write('#define S_COLOR_RED "^1"\n')
        src = SELF.replace('LONGTEXT', 'x' * (CMD_MAX + 10)).replace('SAYTEXT', 'y' * (SAY_MAX + 10))
        open(os.path.join(tmp, 'codemp/game/t.c'), 'w').write(src)
        want = collections.Counter()
        for n, line in enumerate(src.split('\n'), 1):
            m = re.search(r'/\*BAD (\w+|@@@)\*/', line)
            if m:
                want[n] += 1
        r = subprocess.run([sys.executable, os.path.abspath(__file__), '--root', tmp], capture_output=True, text=True)
        got = collections.Counter(int(m.group(1)) for m in re.finditer(r'^FAIL codemp/game/t\.c:(\d+):', r.stdout, re.M))
        other = [l for l in r.stdout.split('\n') if l.startswith('FAIL') and 'codemp/game/t.c:' not in l]
        stale = [l for l in other if 'stale allow entry' in l]   # the self-test tree has none of ALLOW's lines
        if set(got) != set(want) or len(other) != len(stale):
            fail('self-test: flagged lines %s, expected %s%s' % (sorted(got), sorted(want), ('; ' + '; '.join(set(other) - set(stale))) if len(other) != len(stale) else ''))
        return sum(want.values())

if SELFTEST:
    cases = selftest()
    print('%d FAILED' % bad if bad else 't_textlint: ALL OK (%d texts checked, self-test %d cases)' % (stats['texts'], cases))
else:
    print('%d FAILED' % bad if bad else 't_textlint: ALL OK (%d texts checked)' % stats['texts'])
sys.exit(1 if bad else 0)
