#!/usr/bin/env python3
# GLX_U: tests scripts/gla_append.py on tiny GLAs built here: the same-skeleton mode, and --drop-extra-bones with
# its checks (the skeleton, the reference frames, one-frame flips) and its output.
#
#   python3 tests/gla/t_gla_append.py
#
# No game files needed. Every case runs the script as a program, the way it is used.

import math
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', '..', 'scripts', 'gla_append.py')
sys.dont_write_bytecode = True	# no __pycache__ left in scripts/
sys.path.insert(0, os.path.join(HERE, '..', '..', 'scripts'))
import gla_append as G  # noqa: E402  (its reader, to look at the outputs)

fails = 0
checks = 0


def check(ok, what):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        print('FAIL', what)


def comp(angle=0.0, axis=2, t=(0.0, 0.0, 0.0)):
    """a compressed bone: a rotation of angle (radians) about one axis, and a translation"""
    q = [math.cos(angle / 2), 0.0, 0.0, 0.0]
    q[1 + axis] = math.sin(angle / 2)
    v = [int(round((c + 2.0) * 16383)) for c in q] + [int(round((c + 512.0) * 64)) for c in t]
    return struct.pack('<7H', *v)


def gla(bones, frames, scale=0.64, base=None, flags=None):
    """bones: [(name, parent)]; frames: [[compressed bone per bone]]; base: per bone 12 floats"""
    nb = len(bones)
    recs = []
    for b, (name, parent) in enumerate(bones):
        kids = [k for k, (_, p) in enumerate(bones) if p == b]
        pose = base[b] if base else (scale, 0, 0, 0, 0, scale, 0, 0, 0, 0, scale, 0)
        inv = tuple(1 / scale if k in (0, 5, 10) else 0.0 for k in range(12))
        recs.append(name.encode().ljust(64, b'\0') + struct.pack('<Ii', flags[b] if flags else 0, parent)
                    + struct.pack('<12f', *pose) + struct.pack('<12f', *inv)
                    + struct.pack('<i', len(kids)) + b''.join(struct.pack('<i', k) for k in kids))
    offsets, skel = [], b''
    for r in recs:
        offsets.append(4 * nb + len(skel))
        skel += r
    skel = b''.join(struct.pack('<i', o) for o in offsets) + skel
    ofs_frames = G.HEADER.size + len(skel)
    pool, known, table = b'', {}, b''
    for fr in frames:
        assert len(fr) == nb
        for c in fr:
            if c not in known:
                known[c] = len(pool) // 14
                pool += c
            i = known[c]
            table += bytes((i & 0xff, (i >> 8) & 0xff, (i >> 16) & 0xff))
    ofs_pool = (ofs_frames + len(table) + 3) & ~3
    body = skel + table + bytes(ofs_pool - ofs_frames - len(table)) + pool
    head = G.HEADER.pack(b'2LGA', 6, b'models/players/_humanoid/_humanoid'.ljust(64, b'\0'), scale, len(frames),
                         ofs_frames, nb, ofs_pool, G.HEADER.size, G.HEADER.size + len(body))
    return head + body


def run(*args):
    p = subprocess.run([sys.executable, SCRIPT] + [str(a) for a in args], capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def frames_of(path, bones=None):
    data = open(path, 'rb').read()
    g = G.parse(path, data)
    n = bones or g['bones']
    return g, [[G.comp(g, f, b) for b in range(n)] for f in range(g['frames'])]


with tempfile.TemporaryDirectory() as tmp:
    def path(name, data):
        p = os.path.join(tmp, name)
        open(p, 'wb').write(data)
        return p
    out = os.path.join(tmp, 'out.gla')

    # ours: 3 bones, 6 frames of small rotations; the sources hold the same 6 frames first, then 4 new ones
    OURS_BONES = [('model_root', -1), ('pelvis', 0), ('spine', 1)]
    BIG_BONES = OURS_BONES + [('ltarsal', 1), ('finger', 2)]
    base_frames = [[comp(0.05 * f), comp(0.1 * f, 0, (1, 2, 3)), comp(-0.07 * f, 1, (0, 0, 10))] for f in range(6)]
    new_frames = [[comp(0.4 + 0.1 * f), comp(0.2, 0, (1, 2, 3)), comp(0.3 * f, 1, (0, 0, 10))] for f in range(4)]
    extra = lambda f: [comp(0.2 * f, 2), comp(-0.1 * f, 0)]
    ours = path('ours.gla', gla(OURS_BONES, base_frames))
    same = path('same.gla', gla(OURS_BONES, base_frames + new_frames))
    big_frames = [fr + extra(f) for f, fr in enumerate(base_frames + new_frames)]
    big_base = [(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)] * 5	# unscaled, as JKG's
    big = path('big.gla', gla(BIG_BONES, big_frames, base=big_base, flags=[0, 0, 1, 0, 0]))

    # 1. the same-skeleton mode, as before
    rc, txt = run(ours, same, 6, 4, out, 6)
    g, fr = frames_of(out)
    check(rc == 0 and g['frames'] == 10 and g['bones'] == 3 and fr[:6] == base_frames and fr[6:] == new_frames,
          'same skeleton: appended (%d) %s' % (rc, txt))
    rc, txt = run(ours, same, 6, 4, out, 7)
    check(rc != 0 and 'expected 7' in txt, 'the frame-count guard: %s' % txt)

    # 2. a bigger skeleton without the option: refused, as before
    rc, txt = run(ours, big, 6, 4, out)
    check(rc != 0 and 'skeletons differ' in txt, 'bigger skeleton without the option refused: %s' % txt)

    # 3. --drop-extra-bones with --ref: copied without the extra bones
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours, big, 6, 4, out, 6)
    g, fr = frames_of(out)
    check(rc == 0 and g['frames'] == 10 and g['bones'] == 3, 'dropped: appended (%d) %s' % (rc, txt))
    check(fr[:6] == base_frames, 'dropped: the old frames unchanged')
    check(fr[6:] == [f[:3] for f in big_frames[6:]], 'dropped: the new frames are the source\'s first 3 bones')
    check(G.skeleton(g) == G.skeleton(G.parse(ours, open(ours, 'rb').read())), 'dropped: our skeleton kept')
    check('6 of 6 match' in txt and 'dropped: ltarsal, finger' in txt and 'flags differ (kept ours): spine' in txt
          and 'base poses differ on 3 of 3 bones' in txt, 'dropped: the notes: %s' % txt)

    # 4. the option and --ref go together
    rc, txt = run('--drop-extra-bones', ours, big, 6, 4, out)
    check(rc != 0 and 'usage' in txt, '--drop-extra-bones without --ref: %s' % txt)
    rc, txt = run('--ref', 0, 6, ours, big, 6, 4, out)
    check(rc != 0 and 'usage' in txt, '--ref without --drop-extra-bones: %s' % txt)
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours, same, 6, 4, out)
    check(rc != 0 and 'not more than our 3' in txt, '--drop-extra-bones with an equal skeleton: %s' % txt)

    # 5. the shared bones must match: a renamed one, a re-parented one, one of ours hanging off an extra bone
    renamed = path('renamed.gla', gla([('model_root', -1), ('hips', 0), ('spine', 1), ('ltarsal', 1), ('finger', 2)], big_frames))
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours, renamed, 6, 4, out)
    check(rc != 0 and 'differ at bone 1' in txt, 'a renamed bone refused: %s' % txt)
    reparented = path('reparented.gla', gla([('model_root', -1), ('pelvis', 0), ('spine', 0), ('ltarsal', 1), ('finger', 2)], big_frames))
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours, reparented, 6, 4, out)
    check(rc != 0 and 'differ at bone 2' in txt, 'a re-parented bone refused: %s' % txt)
    hanging_bones = [('model_root', -1), ('pelvis', 0), ('spine', 3)]
    ours_hang = path('ours_hang.gla', gla(hanging_bones, base_frames))
    big_hang = path('big_hang.gla', gla(hanging_bones + [('ltarsal', 1), ('finger', 2)], big_frames))
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours_hang, big_hang, 6, 4, out)
    check(rc != 0 and 'hangs off a bone we would drop' in txt, 'one of ours hanging off an extra bone refused: %s' % txt)

    # 6. the reference frames: one of 6 different (83%) is refused; a range past either file is refused
    off_frames = [fr[:] for fr in big_frames]
    off_frames[2][1] = comp(1.5, 0, (1, 2, 3))
    off = path('off.gla', gla(BIG_BONES, off_frames))
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours, off, 6, 4, out)
    check(rc != 0 and '5 of 6 match' in txt and 'fewer than 98%' in txt, 'a poor reference refused: %s' % txt)
    rc, txt = run('--drop-extra-bones', '--ref', 0, 7, ours, big, 6, 4, out)
    check(rc != 0 and 'both files must hold frames 0-6' in txt, 'a reference past our frames refused: %s' % txt)

    # 7. a one-frame flip in the frames to copy (frame 7, bone pelvis, 180 degrees) is refused; next to it is fine
    flip_frames = [fr[:] for fr in big_frames]
    flip_frames[6][1] = flip_frames[8][1] = comp(0.2, 0, (1, 2, 3))
    flip_frames[7][1] = comp(0.2 + math.pi, 0, (1, 2, 3))
    flip = path('flip.gla', gla(BIG_BONES, flip_frames))
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours, flip, 6, 4, out)
    check(rc != 0 and 'frame 7 bone pelvis' in txt, 'a one-frame flip refused: %s' % txt)
    rc, txt = run('--drop-extra-bones', '--ref', 0, 6, ours, flip, 9, 1, out)
    check(rc == 0, 'frames after the flip copy: %s' % txt)

print('%d FAILED' % fails if fails else 't_gla_append: ALL OK (%d checks)' % checks)
sys.exit(1 if fails else 0)
