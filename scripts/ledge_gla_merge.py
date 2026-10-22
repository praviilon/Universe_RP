#!/usr/bin/env python3
# GLX_U: [Ledge] appends OJP Enhanced's five ledge-grab animations (GPL-2, the same frames JA+ shipped)
# to our _humanoid.gla, after its last frame.
#
#   python3 scripts/ledge_gla_merge.py <our _humanoid.gla> <OJP _humanoid.gla> <output .gla>
#
# OJP's frames 21376-21448 (73: BOTH_LEDGE_GRAB/HOLD/LEFT/RIGHT/MERCPULL) become frames 22664-22736 here;
# animation.cfg names them. The script refuses unless both skeletons match bone for bone and our file
# has exactly the 22664 frames it was written for, and it checks its output: every existing frame
# decodes byte-identical to the input, every new frame to OJP's.
#
# GLA (mdxa version 6) layout: a 100-byte header (ident, version, name[64], fScale, numFrames,
# ofsFrames, numBones, ofsCompBonePool, ofsSkel, ofsEnd), the skeleton, then per frame and bone a
# 3-byte index into a pool of 14-byte compressed bones (4-byte aligned), which runs to the end.

import struct
import sys

HEADER = struct.Struct('<4si64sfiiiiii')
OUR_FRAMES = 22664          # the file this was written for
OJP_FIRST = 21376           # BOTH_LEDGE_GRAB in OJP's animation.cfg
COUNT = 73                  # through BOTH_LEDGE_MERCPULL's last frame (21448)
COMP = 14


def load(path):
    data = open(path, 'rb').read()
    h = HEADER.unpack_from(data, 0)
    g = dict(zip(('ident', 'version', 'name', 'scale', 'frames', 'ofsFrames', 'bones',
                  'ofsPool', 'ofsSkel', 'ofsEnd'), h))
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
    if len(sys.argv) != 4:
        sys.exit(__doc__ or 'usage: ledge_gla_merge.py ours.gla ojp.gla out.gla')
    ours, ojp = load(sys.argv[1]), load(sys.argv[2])

    if ours['frames'] != OUR_FRAMES:
        sys.exit('ours has %d frames, expected %d (already merged?)' % (ours['frames'], OUR_FRAMES))
    if ojp['frames'] < OJP_FIRST + COUNT:
        sys.exit('OJP file too short')
    if ours['bones'] != ojp['bones'] or ours['scale'] != ojp['scale'] or skeleton(ours) != skeleton(ojp):
        sys.exit('skeletons differ')

    bones = ours['bones']
    pool_old = ours['data'][ours['ofsPool']:]
    pool_count = len(pool_old) // COMP
    known = {}
    for i in range(pool_count):
        known.setdefault(pool_old[i * COMP:(i + 1) * COMP], i)

    new_pool = bytearray()
    new_index = bytearray()
    for f in range(COUNT):
        for b in range(bones):
            c = comp(ojp, OJP_FIRST + f, b)
            i = known.get(c)
            if i is None:
                i = pool_count + len(new_pool) // COMP
                known[c] = i
                new_pool += c
            if i >= 1 << 24:
                sys.exit('pool index overflow')
            new_index += bytes((i & 0xff, (i >> 8) & 0xff, (i >> 16) & 0xff))

    frames = ours['frames'] + COUNT
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
    res = {'data': bytes(out)}
    res.update(zip(('ident', 'version', 'name', 'scale', 'frames', 'ofsFrames', 'bones', 'ofsPool', 'ofsSkel',
                    'ofsEnd'), HEADER.unpack_from(out, 0)))
    if skeleton(res) != skeleton(ours) or d[HEADER.size:ours['ofsFrames']] != out[HEADER.size:ours['ofsFrames']]:
        sys.exit('check failed: skeleton')
    for f in range(ours['frames']):
        for b in range(bones):
            if comp(res, f, b) != comp(ours, f, b):
                sys.exit('check failed: frame %d bone %d changed' % (f, b))
    for f in range(COUNT):
        for b in range(bones):
            if comp(res, ours['frames'] + f, b) != comp(ojp, OJP_FIRST + f, b):
                sys.exit('check failed: new frame %d bone %d' % (f, b))

    open(sys.argv[3], 'wb').write(out)
    print('%d -> %d frames, %d new pool entries, %d -> %d bytes; ledge frames %d-%d'
          % (ours['frames'], frames, len(new_pool) // COMP, len(d), len(out), ours['frames'], frames - 1))


if __name__ == '__main__':
    main()
