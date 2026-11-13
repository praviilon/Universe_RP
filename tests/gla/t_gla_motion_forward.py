#!/usr/bin/env python3
# GLX_U: [GLA Motion] tests scripts/gla_motion_forward.py on tiny GLAs built here, then checks the repo's
# _humanoid.gla has no animation left whose Motion bone points backwards throughout.
#
#   python3 tests/gla/t_gla_motion_forward.py
#
# Every case runs the script as a program, the way it is used. The outputs are read with gla_append.py's reader
# and chained here, not with the script's own code.

import math
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
SCRIPT = os.path.join(ROOT, 'scripts', 'gla_motion_forward.py')
HUMANOID = os.path.join(ROOT, 'assets', 'client', 'models', 'players', '_humanoid')
sys.dont_write_bytecode = True	# no __pycache__ left in scripts/
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import gla_append as G  # noqa: E402

fails = 0
checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print('FAIL', what)


def qmul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2, w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2)


def qaxis(angle, axis):
    q = [math.cos(angle / 2), 0.0, 0.0, 0.0]
    q[1 + axis] = math.sin(angle / 2)
    return tuple(q)


def comp(q, t=(0.0, 0.0, 0.0)):
    return struct.pack('<7H', *([int(round((c + 2.0) * 16383)) for c in q] + [int(round((c + 512.0) * 64)) for c in t]))


SCALE = 0.64
MOTION_BASE = (SCALE, 0, 0, 0, 0, SCALE, 0, 0.4, 0, 0, SCALE, 36.0)
UNIT = (SCALE, 0, 0, 0, 0, SCALE, 0, 0, 0, 0, SCALE, 0)


def gla(bones, frames, base):
    """bones: [(name, parent)]; frames: [[compressed bone per bone]]; base: per bone 12 floats"""
    nb = len(bones)
    recs = []
    for b, (name, parent) in enumerate(bones):
        kids = [k for k, (_, p) in enumerate(bones) if p == b]
        inv = tuple(1 / SCALE if k in (0, 5, 10) else 0.0 for k in range(12))
        recs.append(name.encode().ljust(64, b'\0') + struct.pack('<Ii', 0, parent) + struct.pack('<12f', *base[b])
                    + struct.pack('<12f', *inv) + struct.pack('<i', len(kids)) + b''.join(struct.pack('<i', k) for k in kids))
    offsets, skel = [], b''
    for r in recs:
        offsets.append(4 * nb + len(skel))
        skel += r
    skel = b''.join(struct.pack('<i', o) for o in offsets) + skel
    ofs_frames = G.HEADER.size + len(skel)
    pool, known, table = b'', {}, b''
    for fr in frames:
        for c in fr:
            if c not in known:
                known[c] = len(pool) // 14
                pool += c
            i = known[c]
            table += bytes((i & 0xff, (i >> 8) & 0xff, (i >> 16) & 0xff))
    ofs_pool = (ofs_frames + len(table) + 3) & ~3
    body = skel + table + bytes(ofs_pool - ofs_frames - len(table)) + pool
    head = G.HEADER.pack(b'2LGA', 6, b'models/players/_humanoid/_humanoid'.ljust(64, b'\0'), SCALE, len(frames),
                         ofs_frames, nb, ofs_pool, G.HEADER.size, G.HEADER.size + len(body))
    return head + body


def run(*args):
    p = subprocess.run([sys.executable, SCRIPT] + [str(a) for a in args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def bolts(path, mo):
    """the Motion bone's bolt (chained, times its base pose) in every frame"""
    g = G.parse(path, open(path, 'rb').read())
    base = struct.unpack_from('<12f', G.skeleton(g)[mo], 72)
    ch = G.Chainer(g, g['bones'])
    return g, [G.mul(ch.frame(f)[mo], base) for f in range(g['frames'])]


BONES = [('model_root', -1), ('pelvis', 0), ('Motion', 1), ('spine', 1)]
BASES = [UNIT, UNIT, MOTION_BASE, UNIT]
MO = 2


def frame(f, back, noise=0.0):
    """root turned a little, the pelvis tilted; the Motion bone undoes both (forward), or then turns 180 (back)"""
    root, pelvis = qaxis(0.03 * f, 2), qaxis(0.1 + 0.05 * f, 0)
    undo = qmul(qaxis(-(0.1 + 0.05 * f), 0), qaxis(-0.03 * f + noise, 2))
    motion = qmul(undo, qaxis(math.pi, 2)) if back else undo
    return [comp(root, (0, 0, 1)), comp(pelvis, (1, 2, 30)), comp(motion, (0.5 * f, -2, 3)),
            comp(qaxis(0.2 * f, 1), (0, 0, 8))]


with tempfile.TemporaryDirectory() as tmp:
    def path(name, data):
        p = os.path.join(tmp, name)
        open(p, 'wb' if isinstance(data, bytes) else 'w').write(data)
        return p
    out, out2 = os.path.join(tmp, 'out.gla'), os.path.join(tmp, 'out2.gla')

    # frames: 0-1 BOTH_STAND1 (forward); 2-4 BOTH_LEFT (back throughout); 5-8 BOTH_FLIP (back in 6 and 7 only);
    # 9-10 BOTH_SHARED (back throughout) whose frame 10 is also TORSO_SHARED's, with frame 11 forward; 12 in no
    # animation, back
    BACK = {2, 3, 4, 6, 7, 9, 10, 12}
    frames = [frame(f, f in BACK, 0.01 if f == 3 else 0.0) for f in range(13)]
    cfg = path('animation.cfg', '// test\nBOTH_STAND1\t0\t2\t-1\t20\nBOTH_LEFT\t2\t3\t-1\t20\n'
               'BOTH_FLIP\t5\t4\t-1\t20\nBOTH_SHARED\t9\t2\t-1\t20\nTORSO_SHARED\t10\t2\t-1\t20\n'
               'BOTH_EMPTY\t0\t-1\t-1\t20\n')
    src = path('src.gla', gla(BONES, frames, BASES))

    # 1. the backwards animation is turned forward, nothing else changes
    rc, txt = run(src, cfg, out)
    check(rc == 0 and 'turned forward: BOTH_LEFT' in txt and 'turned forward: BOTH_SHARED (frames 9-10, 1 of them)' in txt
          and 'backwards in 2 of 4 frames: BOTH_FLIP' in txt and 'frames [10]' in txt and '2 animations, 4 frames' in txt,
          'the run: (%d) %s' % (rc, txt))
    if rc == 0:
        g0, b0 = bolts(src, MO)
        g1, b1 = bolts(out, MO)
        changed = {f for f in range(13) for b in range(4) if G.comp(g0, f, b) != G.comp(g1, f, b)}
        bones_changed = {b for f in range(13) for b in range(4) if G.comp(g0, f, b) != G.comp(g1, f, b)}
        check(changed == {2, 3, 4, 9} and bones_changed == {MO}, 'changed: frames %s, bones %s' % (changed, bones_changed))
        check(G.skeleton(g0) == G.skeleton(g1) and g1['frames'] == 13 and g1['ofsPool'] == g0['ofsPool'],
              'skeleton and layout kept')
        for f in (2, 3, 4, 9):
            rot = max(abs(b1[f][k] - b1[0][k]) for k in (0, 1, 2, 4, 5, 6, 8, 9, 10)) / SCALE
            pos = max(abs(b1[f][k] - b0[f][k]) for k in (3, 7, 11))
            check(rot < 0.002 and pos < 0.05, 'frame %d: forward (%.5f) at its old position (%.4f)' % (f, rot, pos))
        check(abs(b0[2][0] / SCALE + 1) < 0.01 and abs(b0[2][5] / SCALE + 1) < 0.01, 'the test frame 2 was backwards')
        # 2. a second run finds nothing to change
        rc, txt = run(out, cfg, out2)
        check(rc == 0 and 'nothing to change' in txt and open(out, 'rb').read() == open(out2, 'rb').read(),
              'a second run: (%d) %s' % (rc, txt))

    # 3. a file already forward comes out byte for byte
    fwd = path('fwd.gla', gla(BONES, [frame(f, False) for f in range(13)], BASES))
    rc, txt = run(fwd, cfg, out)
    check(rc == 0 and 'nothing to change' in txt and open(fwd, 'rb').read() == open(out, 'rb').read(),
          'already forward: (%d) %s' % (rc, txt))

    # 4. refused: a bone hanging off Motion; no Motion bone; no BOTH_STAND1; an animation past the frames
    kid = path('kid.gla', gla(BONES[:3] + [('spine', 2)], frames, BASES))
    rc, txt = run(kid, cfg, out)
    check(rc != 0 and 'bones hang off Motion (spine)' in txt, 'a child of Motion refused: %s' % txt)
    nomo = path('nomo.gla', gla([('model_root', -1), ('pelvis', 0), ('motion2', 1), ('spine', 1)], frames, BASES))
    rc, txt = run(nomo, cfg, out)
    check(rc != 0 and 'no Motion bone' in txt, 'no Motion bone refused: %s' % txt)
    rc, txt = run(src, path('nostand.cfg', 'BOTH_LEFT\t2\t3\t-1\t20\n'), out)
    check(rc != 0 and 'no BOTH_STAND1' in txt, 'no BOTH_STAND1 refused: %s' % txt)
    rc, txt = run(src, path('past.cfg', 'BOTH_STAND1\t0\t2\t-1\t20\nBOTH_LEFT\t12\t3\t-1\t20\n'), out)
    check(rc != 0 and 'BOTH_LEFT has frames 12-14' in txt, 'frames past the .gla refused: %s' % txt)

# 5. the repo's .gla: nothing left to change
with tempfile.TemporaryDirectory() as tmp:
    gla_path = os.path.join(HUMANOID, '_humanoid.gla')
    out = os.path.join(tmp, 'out.gla')
    rc, txt = run(gla_path, os.path.join(HUMANOID, 'animation.cfg'), out)
    check(rc == 0 and 'nothing to change' in txt, 'the repo .gla has no animation backwards throughout: (%d) %s' % (rc, txt))

print('%d FAILED' % fails if fails else 't_gla_motion_forward: ALL OK (%d checks)' % checks)
sys.exit(1 if fails else 0)
