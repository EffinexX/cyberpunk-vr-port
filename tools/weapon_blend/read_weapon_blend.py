"""Reads the boxes drawn in a .blend made by make_weapon_blend.py and writes them for CyberpunkVRPort_HandCollision. Run inside Blender:

    blender -b <file.blend> --python tools/weapon_blend/read_weapon_blend.py -- <out weapon_custom_boxes.lua>

Every .blend given in turn adds to the file: weapons and attachments already in it and not in this .blend are kept.

A box is a mesh object named box... in a weapon's or an attachment's collection: its own bounding box, as placed. Written as
{ cx, cy, cz, hx, hy, hz, qi, qj, qk, qr }: middle, half sizes, and the turn, in the weapon's frame (origin at the grip, +Y along the
barrel, +Z up, metres) -- for an attachment in its mount's frame. A box with shear (scaled after turning under a turned parent) cannot
be one box and is refused, as is a box of no size.
"""
import os
import re
import sys

import bpy

out = os.path.abspath(sys.argv[sys.argv.index("--") + 1])
problems = []


def world(ob):
    """From the stored parts, not matrix_world: that is stale for what sits in a hidden collection."""
    m = ob.matrix_parent_inverse @ ob.matrix_basis
    return world(ob.parent) @ m if ob.parent else ob.matrix_basis.copy()


def box_of(ob, frame_inv):
    m = frame_inv @ world(ob)
    loc, rot, scale = m.decompose()
    # shear: the matrix rebuilt from the parts must be the matrix
    from mathutils import Matrix
    back = Matrix.LocRotScale(loc, rot, scale)
    if max(abs(back[i][j] - m[i][j]) for i in range(3) for j in range(4)) > 1e-4:
        problems.append("%s: sheared (scaled after a turn of its parent), not written" % ob.name)
        return None
    bb = ob.bound_box
    lo = [min(c[i] for c in bb) for i in range(3)]
    hi = [max(c[i] for c in bb) for i in range(3)]
    mid_local = [(lo[i] + hi[i]) * 0.5 for i in range(3)]
    half = [abs((hi[i] - lo[i]) * 0.5 * scale[i]) for i in range(3)]
    if min(half) < 1e-4:
        problems.append("%s: no size, not written" % ob.name)
        return None
    from mathutils import Vector
    mid = m @ Vector(mid_local)
    q = rot.normalized()
    if q.w < 0:
        q = -q
    return [mid.x, mid.y, mid.z, half[0], half[1], half[2], q.x, q.y, q.z, q.w]


def boxes_in(coll, frame_inv):
    found = []
    for ob in sorted(coll.objects, key=lambda o: o.name):
        if ob.type == "MESH" and ob.name.lower().startswith("box"):
            b = box_of(ob, frame_inv)
            if b:
                found.append(b)
    return found


# what the file holds already
weapons, attachments = {}, {}
if os.path.exists(out):
    section = None
    name = None
    with open(out, encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if s.startswith("weapons = {"):
                section = weapons
            elif s.startswith("attachments = {"):
                section = attachments
            m = re.match(r"([a-z0-9_]+) = \{$", s)
            if m and section is not None and m.group(1) not in ("weapons", "attachments", "boxes"):
                name = m.group(1)
                section[name] = {"boxes": [], "muzzle": None, "scope": None}
            m = re.match(r"(muzzle|scope) = \{(.*)\},$", s)
            if m and name:
                section[name][m.group(1)] = [float(x) for x in m.group(2).split(",")]
            m = re.match(r"\{([-0-9., ]+)\},$", s)
            if m and name:
                section[name]["boxes"].append([float(x) for x in m.group(1).split(",")])

from mathutils import Matrix
for coll in bpy.data.collections:
    kind = coll.get("vrp_kind")
    if kind == "weapon":
        mount = bpy.data.objects.get(coll.name + "/muzzle")
        top = bpy.data.objects.get(coll.name + "/scope")
        boxes = boxes_in(coll, Matrix.Identity(4))
        if not boxes:
            problems.append("%s: no boxes, left as it was" % coll.name)
            continue
        weapons[coll.name] = {"boxes": boxes, "muzzle": list(world(mount).translation) if mount else None,
                              "scope": list(world(top).translation) if top else None}
    elif kind == "attachment":
        root = bpy.data.objects.get(coll.name + "/mount")
        if not root:
            problems.append("%s: its mount empty is gone, not written" % coll.name)
            continue
        # in the attachment's own frame: it goes on exactly at the weapon's mount, so its own mount empty is not an offset (and is locked)
        boxes = boxes_in(coll, world(root).inverted())
        if boxes:
            attachments[coll.name] = {"boxes": boxes, "muzzle": None}

f6 = lambda v: ", ".join("%.4f" % x for x in v)
lines = [
    "-- WRITTEN by tools/weapon_blend/read_weapon_blend.py from boxes drawn in Blender -- edit the .blend, not this.",
    "--",
    "-- Hand-made collision boxes, used before the generated ones (weapon_shapes.lua). Each box { cx, cy, cz, hx, hy, hz, qi, qj, qk, qr }:",
    "-- middle, half sizes, turn. A weapon's in its own frame (origin at the grip, +Y along the barrel, +Z up, metres), with `muzzle` and",
    "-- `scope` where those attachments go on; an attachment's in its mount's frame, the same on every weapon.",
    "return {",
    "    weapons = {",
]
for name in sorted(weapons):
    w = weapons[name]
    lines.append("        %s = {" % name)
    for slot in ("muzzle", "scope"):
        if w.get(slot):
            lines.append("            %s = { %s }," % (slot, f6(w[slot])))
    lines.append("            boxes = {")
    for b in w["boxes"]:
        lines.append("                { %s }," % f6(b))
    lines.append("            },")
    lines.append("        },")
lines += ["    },", "    attachments = {"]
for name in sorted(attachments):
    lines.append("        %s = {" % name)
    lines.append("            boxes = {")
    for b in attachments[name]["boxes"]:
        lines.append("                { %s }," % f6(b))
    lines.append("            },")
    lines.append("        },")
lines += ["    },", "}"]
with open(out, "w", newline="\r\n", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")
for p in problems:
    print("PROBLEM", p)
print("weapons", len(weapons), "attachments", len(attachments), "->", out)
