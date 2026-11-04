#!/usr/bin/env python3
# GLX_U: appends frames of another _humanoid.gla to ours, after its last frame.
#
#   python3 scripts/gla_append.py [--drop-extra-bones --ref <first> <count>]
#           <our .gla> <source .gla> <first source frame> <count> <output .gla> [our frame count]
#
# The source's frames first..first+count-1 become our frames N..N+count-1 (N = our frame count before);
# animation.cfg names them. The script refuses unless both skeletons match bone for bone (and, when the last
# argument is given, our file has exactly that many frames, so a run can't be applied twice), and it checks
# its output: every existing frame decodes byte-identical to the input, every new frame to the source's.
#
# --drop-extra-bones: the source's skeleton may be ours plus more bones (JKG's has 73 bones to our 53: toe and
# finger joints). A frame stores each bone relative to its parent (G2_TransformBone, rd-vanilla/tr_ghoul2.cpp:
# a bone's matrix is its parent's times its own frame data), and the base pose is used only for bone overrides
# and bolts, so a frame can be copied without the extra bones as long as none of ours hangs off one. Checks:
#   - our bones are the source's first ones, with the same names and parents, index for index; every extra bone
#     comes after them and none of ours has one as its parent; the scale is the same. Base poses and flags may
#     differ (listed, not refused): we keep ours.
#   - --ref <first> <count> (required): over frames both files hold the same animation (the stock ones), every
#     frame's bones are chained as the engine chains them; at least 98% of the frames must match (rotation within
#     0.02, position within 0.5), so the source's frames mean the same thing ours do.
#   - none of the copied frames has a bone that flips for one frame only (a jump of more than 1.5 in its rotation
#     against both neighbours while the neighbours agree): a glitch a re-exported file can carry.
#   - the output check as above, a new frame being equal to the source's first bones.
#
# Runs so far, in order:
#   [Ledge] OJP Enhanced's ledge grab (GPL-2, the same frames JA+ shipped), from OJP's ojpenhanced GLA:
#       gla_append.py _humanoid.gla <OJP _humanoid.gla> 21376 73 out.gla 22664      -> frames 22664-22736
#   [Couple Emotes] JA+'s kiss (BOTH_KISSEE/KISSER/KISSER1STOP), from TaystJK's assets/japro GLA (JA++ ships
#   the identical file):
#       gla_append.py _humanoid.gla <JA+ _humanoid.gla> 21376 144 out.gla 22737     -> frames 22737-22880
#   [JA+ Anims] JA+'s moves 1501-1522 (melee kicks, stance changes, back falls, backflip attack, stabs), from the
#   same file:
#       gla_append.py _humanoid.gla <JA+ _humanoid.gla> 21593 781 out.gla 22881     -> frames 22881-23661
#   [JKG Anims] Jedi Knight Galaxies' telekinesis set, from JKG's _humanoid.gla (73 bones, 23616 frames; its
#   stock frames 0-21373 match ours in 21228 of 21374): the target's levitated, idle and land, then the caster's
#   lift, levitate, idle, wave and done:
#       gla_append.py --drop-extra-bones --ref 0 21374 _humanoid.gla <JKG _humanoid.gla> 21621 28 out.gla 23662
#                                                                                   -> frames 23662-23689
#       gla_append.py --drop-extra-bones --ref 0 21374 _humanoid.gla <JKG _humanoid.gla> 22981 38 out.gla 23690
#                                                                                   -> frames 23690-23727
#
# GLA (mdxa version 6) layout: a 100-byte header (ident, version, name[64], fScale, numFrames,
# ofsFrames, numBones, ofsCompBonePool, ofsSkel, ofsEnd), the skeleton, then per frame and bone a
# 3-byte index into a pool of 14-byte compressed bones (4-byte aligned), which runs to the end.

import struct
import sys

HEADER = struct.Struct('<4si64sfiiiiii')
FIELDS = ('ident', 'version', 'name', 'scale', 'frames', 'ofsFrames', 'bones', 'ofsPool', 'ofsSkel', 'ofsEnd')
COMP = 14
USAGE = ('usage: gla_append.py [--drop-extra-bones --ref <first> <count>] <our .gla> <source .gla> '
         '<first source frame> <count> <output .gla> [our frame count]')

REF_MATCH = 0.98		# share of the reference frames that must match
REF_ROT = 0.02			# largest difference in a chained bone's rotation matrix
REF_POS = 0.5			# and in its translation
FLIP_JUMP = 1.5			# a bone's rotation jumping this much against both neighbours...
FLIP_CALM = 0.5			# ...while the neighbours differ by less than this is a one-frame flip


def parse(path, data):
    g = dict(zip(FIELDS, HEADER.unpack_from(data, 0)))
    if g['ident'] != b'2LGA' or g['version'] != 6 or g['ofsEnd'] != len(data):
        sys.exit('%s: not a version 6 GLA' % path)
    if g['ofsPool'] < g['ofsFrames'] + g['frames'] * g['bones'] * 3 or (len(data) - g['ofsPool']) % COMP:
        sys.exit('%s: unexpected layout' % path)
    g['data'] = data
    return g


def skeleton(g):
    """name, flags, parent and base pose matrices of every bone"""
    d, out = g['data'], []
    for b in range(g['bones']):
        off = HEADER.size + struct.unpack_from('<i', d, HEADER.size + 4 * b)[0]
        out.append(d[off:off + 64 + 8 + 96])
    return out


def bone_name(rec):
    return rec[:64].split(b'\0')[0].decode('latin-1')


def bone_parent(rec):
    return struct.unpack_from('<i', rec, 68)[0]


def index(g, frame, bone):
    o = g['ofsFrames'] + (frame * g['bones'] + bone) * 3
    d = g['data']
    return d[o] | d[o + 1] << 8 | d[o + 2] << 16


def comp(g, frame, bone):
    o = g['ofsPool'] + index(g, frame, bone) * COMP
    return g['data'][o:o + COMP]


# --- the engine's bone maths (MC_UnCompressQuat, Multiply_3x4Matrix, G2_TransformBone), for the checks only

def decode(c):
    """a 14-byte compressed bone -> a 3x4 matrix as 12 floats, row by row"""
    v = struct.unpack('<7H', c)
    w, x, y, z = (a / 16383.0 - 2.0 for a in v[:4])
    tx, ty, tz = (a / 64.0 - 512.0 for a in v[4:])
    return (1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y), tx,
            2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x), ty,
            2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y), tz)


def mul(a, b):
    """a * b, both 3x4 (affine)"""
    out = []
    for r in range(3):
        a0, a1, a2, a3 = a[r * 4:r * 4 + 4]
        out += [a0 * b[0] + a1 * b[4] + a2 * b[8],
                a0 * b[1] + a1 * b[5] + a2 * b[9],
                a0 * b[2] + a1 * b[6] + a2 * b[10],
                a0 * b[3] + a1 * b[7] + a2 * b[11] + a3]
    return out


class Chainer:
    """every bone of a frame chained to its parents, the first n bones only"""
    def __init__(self, g, n):
        self.g, self.n = g, n
        self.parents = [bone_parent(r) for r in skeleton(g)[:n]]
        self.order, seen = [], set()
        def visit(b):
            if b in seen:
                return
            p = self.parents[b]
            if p >= 0:
                visit(p)
            seen.add(b)
            self.order.append(b)
        for b in range(n):
            visit(b)
        self.cache = {}

    def frame(self, f):
        g, out = self.g, [None] * self.n
        for b in self.order:
            i = index(g, f, b)
            m = self.cache.get(i)
            if m is None:
                o = g['ofsPool'] + i * COMP
                m = self.cache[i] = decode(g['data'][o:o + COMP])
            p = self.parents[b]
            out[b] = m if p < 0 else mul(out[p], m)
        return out


def rot_diff(a, b):
    return max(abs(a[k] - b[k]) for k in (0, 1, 2, 4, 5, 6, 8, 9, 10))


def pos_diff(a, b):
    return max(abs(a[k] - b[k]) for k in (3, 7, 11))


def check_extra_bones(ours, src):
    """--drop-extra-bones: ours must be the source's first bones; returns the notes to print"""
    so, ss = skeleton(ours), skeleton(src)
    n = ours['bones']
    if src['bones'] <= n:
        sys.exit('--drop-extra-bones: the source has %d bones, not more than our %d' % (src['bones'], n))
    if ours['scale'] != src['scale']:
        sys.exit('skeletons differ: scale %g against %g' % (ours['scale'], src['scale']))
    for b in range(n):
        if bone_name(so[b]) != bone_name(ss[b]) or bone_parent(so[b]) != bone_parent(ss[b]):
            sys.exit('skeletons differ at bone %d: ours %s (parent %d), the source\'s %s (parent %d)'
                     % (b, bone_name(so[b]), bone_parent(so[b]), bone_name(ss[b]), bone_parent(ss[b])))
        if bone_parent(so[b]) >= n:
            sys.exit('bone %d (%s) hangs off a bone we would drop' % (b, bone_name(so[b])))
    notes = []
    flags = [bone_name(so[b]) for b in range(n) if so[b][64:68] != ss[b][64:68]]
    poses = [bone_name(so[b]) for b in range(n) if so[b][72:] != ss[b][72:]]
    if flags:
        notes.append('flags differ (kept ours): ' + ', '.join(flags))
    if poses:
        notes.append('base poses differ on %d of %d bones (kept ours)' % (len(poses), n))
    notes.append('dropped: ' + ', '.join(bone_name(ss[b]) for b in range(n, src['bones'])))
    return notes


def check_reference(ours, src, first, count):
    """the source's frames first..first+count-1 against ours, chained: how many match"""
    n = ours['bones']
    if count < 1 or first < 0 or ours['frames'] < first + count or src['frames'] < first + count:
        sys.exit('--ref: both files must hold frames %d-%d' % (first, first + count - 1))
    co, cs = Chainer(ours, n), Chainer(src, n)
    good, worst = 0, []
    for f in range(first, first + count):
        a, b = co.frame(f), cs.frame(f)
        r = max(rot_diff(a[k], b[k]) for k in range(n))
        p = max(pos_diff(a[k], b[k]) for k in range(n))
        if r <= REF_ROT and p <= REF_POS:
            good += 1
        else:
            worst.append((r, p, f))
    worst.sort(reverse=True)
    share = good / count
    line = 'reference frames %d-%d: %d of %d match (%.1f%%)' % (first, first + count - 1, good, count, share * 100)
    if worst:
        line += '; worst: ' + ', '.join('%d (rotation %.2f, position %.2f)' % (f, r, p) for r, p, f in worst[:5])
    if share < REF_MATCH:
        sys.exit(line + '\nrefused: fewer than %d%% of the reference frames match' % round(REF_MATCH * 100))
    return line


def check_flips(src, n, first, count):
    """refuses a copied frame where a bone flips for that frame only"""
    ch = Chainer(src, n)
    lo, hi = max(first - 1, 0), min(first + count, src['frames'] - 1)
    frames = {f: ch.frame(f) for f in range(lo, hi + 1)}
    flips = []
    for f in range(first, first + count):
        if f - 1 not in frames or f + 1 not in frames:
            continue
        for b in range(n):
            here, prev, nxt = frames[f][b], frames[f - 1][b], frames[f + 1][b]
            if min(rot_diff(here, prev), rot_diff(here, nxt)) > FLIP_JUMP and rot_diff(prev, nxt) < FLIP_CALM:
                flips.append('frame %d bone %s' % (f, bone_name(skeleton(src)[b])))
    if flips:
        sys.exit('refused: one-frame flips in the frames to copy: ' + ', '.join(flips[:20]))


def main():
    args = sys.argv[1:]
    drop, ref = False, None
    while args and args[0].startswith('--'):
        opt = args.pop(0)
        if opt == '--drop-extra-bones':
            drop = True
        elif opt == '--ref' and len(args) >= 2:
            try:
                ref = (int(args.pop(0)), int(args.pop(0)))
            except ValueError:
                sys.exit(USAGE)
        else:
            sys.exit(USAGE)
    if len(args) not in (5, 6) or drop != (ref is not None):
        sys.exit(USAGE)
    ours = parse(args[0], open(args[0], 'rb').read())
    src = parse(args[1], open(args[1], 'rb').read())
    first, count = int(args[2]), int(args[3])
    out_path = args[4]

    if len(args) == 6 and ours['frames'] != int(args[5]):
        sys.exit('ours has %d frames, expected %s (already appended?)' % (ours['frames'], args[5]))
    if count < 1 or first < 0 or src['frames'] < first + count:
        sys.exit('the source has no frames %d-%d' % (first, first + count - 1))

    notes = []
    if drop:
        notes = check_extra_bones(ours, src)
        notes.append(check_reference(ours, src, ref[0], ref[1]))
        check_flips(src, ours['bones'], first, count)
    elif ours['bones'] != src['bones'] or ours['scale'] != src['scale'] or skeleton(ours) != skeleton(src):
        sys.exit('skeletons differ')

    bones = ours['bones']
    pool_old = ours['data'][ours['ofsPool']:]
    pool_count = len(pool_old) // COMP
    known = {}
    for i in range(pool_count):
        known.setdefault(pool_old[i * COMP:(i + 1) * COMP], i)

    new_pool = bytearray()
    new_index = bytearray()
    for f in range(count):
        for b in range(bones):	# the source's first bones: with --drop-extra-bones, its extra ones are left out
            c = comp(src, first + f, b)
            i = known.get(c)
            if i is None:
                i = pool_count + len(new_pool) // COMP
                known[c] = i
                new_pool += c
            if i >= 1 << 24:
                sys.exit('pool index overflow')
            new_index += bytes((i & 0xff, (i >> 8) & 0xff, (i >> 16) & 0xff))

    frames = ours['frames'] + count
    table_end = ours['ofsFrames'] + frames * bones * 3
    ofs_pool = (table_end + 3) & ~3
    d = ours['data']
    out = bytearray(d[:ours['ofsFrames'] + ours['frames'] * bones * 3])
    out += new_index
    out += bytes(ofs_pool - len(out))
    out += pool_old
    out += new_pool
    HEADER.pack_into(out, 0, b'2LGA', 6, ours['name'], ours['scale'], frames, ours['ofsFrames'], bones,
                     ofs_pool, ours['ofsSkel'], len(out))

    # check the result through the same reader
    res = parse('output', bytes(out))
    if skeleton(res) != skeleton(ours) or d[HEADER.size:ours['ofsFrames']] != out[HEADER.size:ours['ofsFrames']]:
        sys.exit('check failed: skeleton')
    for f in range(ours['frames']):
        for b in range(bones):
            if comp(res, f, b) != comp(ours, f, b):
                sys.exit('check failed: frame %d bone %d changed' % (f, b))
    for f in range(count):
        for b in range(bones):
            if comp(res, ours['frames'] + f, b) != comp(src, first + f, b):
                sys.exit('check failed: new frame %d bone %d' % (f, b))

    open(out_path, 'wb').write(out)
    for line in notes:
        print(line)
    print('%d -> %d frames, %d new pool entries, %d -> %d bytes; new frames %d-%d'
          % (ours['frames'], frames, len(new_pool) // COMP, len(d), len(out), ours['frames'], frames - 1))


if __name__ == '__main__':
    main()
