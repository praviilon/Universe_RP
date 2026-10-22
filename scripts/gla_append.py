#!/usr/bin/env python3
# GLX_U: appends frames of another _humanoid.gla (same skeleton) to ours, after its last frame.
#
#   python3 scripts/gla_append.py <our .gla> <source .gla> <first source frame> <count> <output .gla> [our frame count]
#
# The source's frames first..first+count-1 become our frames N..N+count-1 (N = our frame count before);
# animation.cfg names them. The script refuses unless both skeletons match bone for bone (and, when the last
# argument is given, our file has exactly that many frames, so a run can't be applied twice), and it checks
# its output: every existing frame decodes byte-identical to the input, every new frame to the source's.
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
#
# GLA (mdxa version 6) layout: a 100-byte header (ident, version, name[64], fScale, numFrames,
# ofsFrames, numBones, ofsCompBonePool, ofsSkel, ofsEnd), the skeleton, then per frame and bone a
# 3-byte index into a pool of 14-byte compressed bones (4-byte aligned), which runs to the end.

import struct
import sys

HEADER = struct.Struct('<4si64sfiiiiii')
FIELDS = ('ident', 'version', 'name', 'scale', 'frames', 'ofsFrames', 'bones', 'ofsPool', 'ofsSkel', 'ofsEnd')
COMP = 14
USAGE = 'usage: gla_append.py <our .gla> <source .gla> <first source frame> <count> <output .gla> [our frame count]'


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


def index(g, frame, bone):
    o = g['ofsFrames'] + (frame * g['bones'] + bone) * 3
    d = g['data']
    return d[o] | d[o + 1] << 8 | d[o + 2] << 16


def comp(g, frame, bone):
    o = g['ofsPool'] + index(g, frame, bone) * COMP
    return g['data'][o:o + COMP]


def main():
    if len(sys.argv) not in (6, 7):
        sys.exit(USAGE)
    ours = parse(sys.argv[1], open(sys.argv[1], 'rb').read())
    src = parse(sys.argv[2], open(sys.argv[2], 'rb').read())
    first, count = int(sys.argv[3]), int(sys.argv[4])

    if len(sys.argv) == 7 and ours['frames'] != int(sys.argv[6]):
        sys.exit('ours has %d frames, expected %s (already appended?)' % (ours['frames'], sys.argv[6]))
    if count < 1 or first < 0 or src['frames'] < first + count:
        sys.exit('the source has no frames %d-%d' % (first, first + count - 1))
    if ours['bones'] != src['bones'] or ours['scale'] != src['scale'] or skeleton(ours) != skeleton(src):
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
        for b in range(bones):
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

    open(sys.argv[5], 'wb').write(out)
    print('%d -> %d frames, %d new pool entries, %d -> %d bytes; new frames %d-%d'
          % (ours['frames'], frames, len(new_pool) // COMP, len(d), len(out), ours['frames'], frames - 1))


if __name__ == '__main__':
    main()
