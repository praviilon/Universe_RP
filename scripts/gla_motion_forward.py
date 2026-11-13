#!/usr/bin/env python3
# GLX_U: [GLA Motion] turns the Motion bone forward in the animations where it points backwards.
#
#   python3 scripts/gla_motion_forward.py <in .gla> <animation.cfg> <out .gla>
#
# Why: the Motion bone has no mesh and no children; the game reads it in one place only, the stock spine
# correction (BG_G2ClientSpineAngles(), bg_pmove.c, run by cgame and by the server's hit-detection pose). While
# legs and torso play different animations, that code turns the spine back by the Motion bone's angles. Stock
# JA exported some of its left/right twins mirrored, the Motion bone included: in BOTH_FORCELANDLEFT1 and others
# it points backwards (turned 180 degrees about the up axis) in every frame, while the twin's points forward. On
# the frame where legs and torso differ, after the left air kick for one, the spine and head turn round.
#
# What: an animation (an animation.cfg entry) whose Motion bone points backwards in every frame (within
# MIRROR_TOL of BOTH_STAND1's turned 180 degrees about the up axis; that is, in the bolt's own axes, x and y
# negated) gets, in each of its frames, a Motion bone with BOTH_STAND1's orientation at its old position. The
# bone is stored relative to its parent, so the new local transform is inv(parent) * new; it is compressed as
# the engine's MC_UnCompressQuat reads it and added at the end of the compressed-bone pool (shared when equal),
# and only that frame's Motion index is changed.
# Left alone: an animation where the bone points backwards in some frames only (it turns with the body, as in
# BOTH_JUMPFLIPSLASHDOWN1), and any frame that also belongs to an animation not backwards throughout.
#
# The script refuses to write unless:
#   - the Motion bone exists and no bone hangs off it;
#   - the header (but the end offset), the skeleton, the frame table (but the changed Motion indexes) and the old
#     pool are byte-identical, so every other bone of every frame decodes as before;
#   - in every changed frame the Motion bone, chained and bolted as the engine does, reads BOTH_STAND1's
#     orientation within FORWARD_TOL and its old position within POS_TOL;
#   - a second pass over its own output finds nothing left to change.
# Nothing to change: the output is the input, byte for byte.
#
# Run so far:
#   [GLA Motion] gla_motion_forward.py _humanoid.gla animation.cfg out.gla
#       57 animations (924 frames): 13 deaths (BOTH_DEATH4, ...), the left Force jump, air and landing, BOTH_TOSS2,
#       BOTH_FORCEGRIP3THROW, the right wall rebound, hold and release, 5 get-ups, the right wall run, its stop and
#       flips, 15 saber locks (BOTH_LK_*) and 12 vehicle animations (BOTH_VS_*, BOTH_VT_*). Left alone, backwards
#       in some frames only: BOTH_JUMPFLIPSLASHDOWN1, BOTH_JUMPFLIPSTABDOWN, BOTH_BACK_FALLING_GETUP_SLOW.

import math
import os
import struct
import sys

sys.dont_write_bytecode = True	# no __pycache__ left in scripts/
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gla_append import COMP, HEADER, Chainer, bone_name, bone_parent, index, mul, parse, skeleton  # noqa: E402

MOTION = 'Motion'
REF_ANIM = 'BOTH_STAND1'	# its first frame's Motion bone is "forward"
MIRROR_TOL = 2.0		# degrees: backwards = the reference turned 180 about the up axis, within this
FORWARD_TOL = 0.5		# degrees: a changed frame's Motion bone against the reference
POS_TOL = 0.05			# a changed frame's Motion bolt against its old position
FLIP = (-1, -1, 1)		# the bolt's x and y negated: 180 degrees about its z (up)
USAGE = 'usage: gla_motion_forward.py <in .gla> <animation.cfg> <out .gla>'


def read_cfg(path, frames):
    """animation.cfg -> [(name, first, count)], entries with frames only"""
    out = []
    for line in open(path, encoding='latin-1'):
        p = line.split('//')[0].split()
        if len(p) < 2 or not (p[0][0].isalpha() or p[0][0] == '_'):
            continue
        try:
            first, count = int(p[1]), int(p[2])
        except (IndexError, ValueError):
            continue
        if count <= 0:
            continue
        if first < 0 or first + count > frames:
            sys.exit('%s: %s has frames %d-%d, the .gla %d' % (path, p[0], first, first + count - 1, frames))
        out.append((p[0], first, count))
    return out


def inverse(m):
    """the inverse of a 3x4 affine matrix (12 floats, row by row)"""
    a, b, c, d, e, f, g, h, i = m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    r = [(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
         (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]
    t = m[3], m[7], m[11]
    out = []
    for row in range(3):
        rr = r[row * 3:row * 3 + 3]
        out += rr + [-(rr[0] * t[0] + rr[1] * t[1] + rr[2] * t[2])]
    return out


def rot3(m):
    """the 3x3 rotation of a 3x4 matrix, columns scaled to length 1"""
    cols = []
    for c in range(3):
        v = (m[c], m[4 + c], m[8 + c])
        n = math.sqrt(sum(x * x for x in v))
        cols.append([x / n for x in v])
    return [[cols[c][r] for c in range(3)] for r in range(3)]


def angle(a, b, flip=(1, 1, 1)):
    """degrees between rotation a and rotation b with b's columns scaled by flip (a^T b diag(flip))"""
    tr = sum(a[k][i] * b[k][i] * flip[i] for i in range(3) for k in range(3))
    return math.degrees(math.acos(max(-1.0, min(1.0, (tr - 1) / 2))))


def compress(m):
    """a 3x4 rotation + translation -> the 14 bytes MC_UnCompressQuat reads"""
    m00, m01, m02, m10, m11, m12, m20, m21, m22 = m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10]
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1) * 2
        w, x, y, z = s / 4, (m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s
    elif m00 > m11 and m00 > m22:
        s = math.sqrt(1 + m00 - m11 - m22) * 2
        w, x, y, z = (m21 - m12) / s, s / 4, (m01 + m10) / s, (m02 + m20) / s
    elif m11 > m22:
        s = math.sqrt(1 + m11 - m00 - m22) * 2
        w, x, y, z = (m02 - m20) / s, (m01 + m10) / s, s / 4, (m12 + m21) / s
    else:
        s = math.sqrt(1 + m22 - m00 - m11) * 2
        w, x, y, z = (m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, s / 4
    n = math.sqrt(w * w + x * x + y * y + z * z)
    vals = [round((q / n + 2) * 16383) for q in (w, x, y, z)]
    vals += [round((t + 512) * 64) for t in (m[3], m[7], m[11])]
    if min(vals) < 0 or max(vals) > 0xffff:
        sys.exit('a new Motion transform is out of the compressed range')
    return struct.pack('<7H', *vals)


class Motion:
    """the Motion bone of a .gla: its bolt (chained, times the base pose) frame by frame"""
    def __init__(self, g):
        sk = skeleton(g)
        names = [bone_name(r) for r in sk]
        if MOTION not in names:
            sys.exit('no %s bone' % MOTION)
        self.bone = names.index(MOTION)
        kids = [names[b] for b in range(g['bones']) if bone_parent(sk[b]) == self.bone]
        if kids:
            sys.exit('refused: bones hang off %s (%s)' % (MOTION, ', '.join(kids)))
        self.parent = bone_parent(sk[self.bone])
        self.base = struct.unpack_from('<12f', sk[self.bone], 72)	# BasePoseMat
        top, b = self.bone, self.bone
        while b >= 0:			# chain only as far as the Motion bone's parents need
            top, b = max(top, b), bone_parent(sk[b])
        self.chain = Chainer(g, top + 1)

    def bolt(self, f):
        return mul(self.chain.frame(f)[self.bone], self.base)

    def parent_of(self, f):
        if self.parent < 0:
            return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
        return self.chain.frame(f)[self.parent]


def pick(g, anims, mo):
    """(the frames to change, the animations backwards throughout, those backwards in some frames only, the frames
    left alone as shared with an animation not backwards throughout, the reference orientation)"""
    refs = [a for a in anims if a[0] == REF_ANIM]
    if not refs:
        sys.exit('animation.cfg has no %s' % REF_ANIM)
    ref = rot3(mo.bolt(refs[0][1]))
    back = {}
    def is_back(f):
        if f not in back:
            back[f] = angle(ref, rot3(mo.bolt(f)), FLIP) <= MIRROR_TOL
        return back[f]
    full, part = [], []
    for name, first, count in anims:
        k = sum(is_back(f) for f in range(first, first + count))
        if k == count:
            full.append((name, first, count))
        elif k:
            part.append((name, first, count, k))
    frames = set(f for _, first, count in full for f in range(first, first + count))
    keep = set(f for name, first, count in anims if (name, first, count) not in full
               for f in range(first, first + count))
    shared = frames & keep
    return sorted(frames - shared), full, part, sorted(shared), ref


def main():
    if len(sys.argv) != 4:
        sys.exit(USAGE)
    src, cfg, out_path = sys.argv[1:]
    d = open(src, 'rb').read()
    g = parse(src, d)
    anims = read_cfg(cfg, g['frames'])
    mo = Motion(g)
    frames, full, part, shared, ref = pick(g, anims, mo)

    for name, first, count, k in part:
        print('left alone, backwards in %d of %d frames: %s' % (k, count, name))
    if shared:
        print('left alone, shared with an animation not backwards throughout: frames %s' % shared)
    if not frames:
        open(out_path, 'wb').write(d)
        print('nothing to change')
        return

    ref_bolt = mo.bolt([a for a in anims if a[0] == REF_ANIM][0][1])
    inv_base = inverse(mo.base)
    pool_count = (len(d) - g['ofsPool']) // COMP
    known = {}
    for i in range(pool_count):
        known.setdefault(d[g['ofsPool'] + i * COMP:g['ofsPool'] + (i + 1) * COMP], i)
    out = bytearray(d)
    new_pool = bytearray()
    old_pos = {}
    for f in frames:
        b = mo.bolt(f)
        old_pos[f] = (b[3], b[7], b[11])
        want = [ref_bolt[0], ref_bolt[1], ref_bolt[2], b[3],
                ref_bolt[4], ref_bolt[5], ref_bolt[6], b[7],
                ref_bolt[8], ref_bolt[9], ref_bolt[10], b[11]]
        local = mul(inverse(mo.parent_of(f)), mul(want, inv_base))
        c = compress(local)
        i = known.get(c)
        if i is None:
            i = pool_count + len(new_pool) // COMP
            known[c] = i
            new_pool += c
        if i >= 1 << 24:
            sys.exit('pool index overflow')
        o = g['ofsFrames'] + (f * g['bones'] + mo.bone) * 3
        out[o:o + 3] = bytes((i & 0xff, (i >> 8) & 0xff, (i >> 16) & 0xff))
    out += new_pool
    struct.pack_into('<i', out, HEADER.size - 4, len(out))	# ofsEnd, the header's last field

    # the checks, through the same reader
    res = parse('output', bytes(out))
    if out[:HEADER.size - 4] != d[:HEADER.size - 4] or out[HEADER.size:g['ofsFrames']] != d[HEADER.size:g['ofsFrames']]:
        sys.exit('check failed: header or skeleton')
    if out[g['ofsPool']:len(d)] != d[g['ofsPool']:]:
        sys.exit('check failed: the old pool')
    table_end = g['ofsFrames'] + g['frames'] * g['bones'] * 3
    if out[table_end:g['ofsPool']] != d[table_end:g['ofsPool']]:
        sys.exit('check failed: the padding')
    picked = set(frames)
    for f in range(g['frames']):
        for b in range(g['bones']):
            if (f not in picked or b != mo.bone) and index(res, f, b) != index(g, f, b):
                sys.exit('check failed: frame %d bone %d changed' % (f, b))
    rm = Motion(res)
    worst_rot = worst_pos = 0.0
    for f in frames:
        b = rm.bolt(f)
        worst_rot = max(worst_rot, angle(ref, rot3(b)))
        worst_pos = max(worst_pos, max(abs(b[k] - old_pos[f][j]) for j, k in enumerate((3, 7, 11))))
    if worst_rot > FORWARD_TOL or worst_pos > POS_TOL:
        sys.exit('check failed: a changed Motion bone is %.3f degrees off forward, %.4f off its position'
                 % (worst_rot, worst_pos))
    again = pick(res, anims, rm)[0]
    if again:
        sys.exit('check failed: a second pass would still change %d frames' % len(again))

    open(out_path, 'wb').write(out)
    turned = 0
    for name, first, count in full:
        n = sum(f in picked for f in range(first, first + count))
        if n:
            turned += 1
            print('turned forward: %s (frames %d-%d%s)' % (name, first, first + count - 1,
                                                          '' if n == count else ', %d of them' % n))
    print('%d animations, %d frames; %d new pool entries, %d -> %d bytes; worst %.3f degrees off forward, '
          '%.4f off position' % (turned, len(frames), len(new_pool) // COMP, len(d), len(out), worst_rot, worst_pos))


if __name__ == '__main__':
    main()
