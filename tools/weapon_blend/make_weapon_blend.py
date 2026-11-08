"""Builds -- or adds to -- a .blend to draw weapon collision boxes by hand, from the game's own meshes. Run inside Blender:

    blender -b --python tools/weapon_blend/make_weapon_blend.py -- <dir of serialized .mesh.json> <dir of .app.json> <out .blend> <family> [<family> ...]

e.g. family w_handgun__tsunami_nue. Read back with read_weapon_blend.py.

AN EXISTING FILE IS ADDED TO, NEVER REDONE: boxes, mounts and attachments already in it are kept as they are; what is missing is added,
and parts are sorted into shown and optional (below) again.

THE FRAME IS THE MOD'S: origin at the grip, +Y along the barrel, +Z up, metres, which is also Blender's Z up. Skinned weapon meshes
are in bind pose in that frame, so they are placed as they are.

TWO MOUNTS A WEAPON, the empties `<family>/muzzle` and `<family>/scope`: where every muzzle attachment and every scope sits on that
weapon. An attachment is the same on every weapon, so its boxes are drawn once, in its own mount's frame (its mesh's origin), and the
mod puts them at the weapon's mount. Their first places are guessed -- the front of the barrel at its middle, the top of the rail --
and are what to move if an attachment sits wrong. A mount the user has moved is never guessed again.

OPTIONAL PARTS -- what the default appearance does not have (the compensator is Jackie's Nue only), and the rail, which the game shows
with a scope -- go in a hidden collection `<family>/optional`, out of the guesses.

A box is any mesh object whose name starts with "box": its own bounding box, moved, turned and scaled as you like (no shear).
New boxes start as the ones the mod uses today.
"""
import base64
import glob
import json
import os
import re
import struct
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
SRC, APPS, OUT, FAMILIES = argv[0], argv[1], os.path.abspath(argv[2]), [f.lower() for f in argv[3:]]
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DATA = os.path.join(REPO, "mods", "cet", "CyberpunkVRPort_HandCollision")
SKIP = re.compile(r"decal|shadow|mag_|_fx|glass|lod\d|placeholder", re.I)
MUZZLE = re.compile(r"^(w_att_+power_\d+)_\d+$", re.I)
SCOPE = re.compile(r"^(w_att_+scope_[a-z]+_\d+)(_|$)", re.I)
OPTIONAL_ALWAYS = re.compile(r"_rail", re.I)


def read_lua_table(path):
    """weapon_shapes.lua / weapon_attachments.lua: name = { numbers } or name = { { numbers }, ... }."""
    out, cur = {}, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = re.match(r"\s*([a-z0-9_]+)\s*=\s*\{(.*)\},?\s*$", line)
            if m:
                out[m.group(1)] = [float(x) for x in m.group(2).split(",") if x.strip()]
                continue
            m = re.match(r"\s*([a-z0-9_]+)\s*=\s*\{\s*$", line)
            if m:
                cur = out.setdefault(m.group(1), [])
                continue
            m = re.match(r"\s*\{(.*)\},?\s*$", line)
            if m and cur is not None:
                cur.append([float(x) for x in m.group(1).split(",") if x.strip()])
                continue
            if re.match(r"\s*\},?\s*$", line):
                cur = None
    return out


def default_meshes(family):
    """The meshes of the weapon's default appearance, by name; None when the appearance file is not there."""
    path = os.path.join(APPS, family + "__base1_01.app.json")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        a = json.load(f)
    for ap in a["Data"]["RootChunk"].get("appearances", []):
        d = ap.get("Data", ap)
        if d.get("name", {}).get("$value") != "default":
            continue
        names = set()
        for c in d.get("components", []):
            m = c.get("mesh")
            if isinstance(m, dict):
                p = str(m.get("DepotPath", {}).get("$value", "")).replace("\\", "/")
                if p:
                    names.add(p.split("/")[-1].split(".")[0].lower())
        return names
    return None


def mesh_of(path):
    """Positions and triangles of LOD 0, from the serialized mesh's render buffer."""
    with open(path, encoding="utf-8") as f:
        root = json.load(f)["Data"]["RootChunk"]
    blob = root["renderResourceBlob"]["Data"]
    head = blob["header"]
    raw = base64.b64decode(blob["renderBuffer"]["Bytes"])
    scale, offset = head["quantizationScale"], head["quantizationOffset"]
    ib = head["indexBufferOffset"]
    verts, faces = [], []
    for c in head["renderChunkInfos"]:
        if not c.get("lodMask", 1) & 1:
            continue
        n = c["numVertices"]
        if n == 0:
            continue
        offs = c["chunkVertices"]["byteOffsets"]["Elements"]
        first = c["chunkVertices"]["vertexLayout"]["elements"]["Elements"][0]
        if first.get("usage") != "PS_Position" or first.get("type") != "PT_Short4N":
            continue
        stride = (offs[1] - offs[0]) // n
        base = len(verts)
        for i in range(n):
            x, y, z = struct.unpack_from("<hhh", raw, offs[0] + i * stride)
            verts.append((x / 32767.0 * scale["X"] + offset["X"], y / 32767.0 * scale["Y"] + offset["Y"], z / 32767.0 * scale["Z"] + offset["Z"]))
        ci = c["chunkIndices"]
        wide = ci.get("pe") == "IBCT_IndexUInt"
        k = c["numIndices"]
        idx = struct.unpack_from("<%d%s" % (k, "I" if wide else "H"), raw, ib + ci.get("teOffset", 0))
        for t in range(0, k - 2, 3):
            a, b, d = idx[t], idx[t + 1], idx[t + 2]
            if a < n and b < n and d < n and a != b and b != d and a != d:
                faces.append((base + a, base + b, base + d))
    return verts, faces


def link(obj, coll):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    coll.objects.link(obj)


def child_collection(parent, name, hidden=False):
    c = bpy.data.collections.get(name)
    if c is None:
        c = bpy.data.collections.new(name)
        parent.children.link(c)
        c.hide_viewport = hidden
    return c


def mesh_object(name, verts, faces, coll, material):
    me = bpy.data.meshes.new(name)
    me.from_pydata(verts, [], faces)
    me.validate()
    me.materials.append(material)
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    ob.hide_select = True
    return ob


def box_object(name, center, half, coll, parent=None):
    bpy.ops.mesh.primitive_cube_add(size=2.0)
    ob = bpy.context.active_object
    ob.name = name
    ob.rotation_mode = "QUATERNION"
    link(ob, coll)
    if parent is not None:
        ob.parent = parent
    ob.location = center
    ob.scale = half
    ob.display_type = "WIRE"
    ob.show_in_front = True
    ob.color = (1.0, 0.5, 0.0, 1.0)
    return ob


def mount_empty(name, coll, guess, display):
    """The mount: made at the guess, or moved to a new guess while still where the last guess put it."""
    ob = bpy.data.objects.get(name)
    if ob is None:
        ob = bpy.data.objects.new(name, None)
        ob.empty_display_type = display
        ob.empty_display_size = 0.03
        coll.objects.link(ob)
        ob["vrp_guess"] = list(guess)
        ob.location = guess
        return ob, "guessed"
    # a file from before the guess was kept is the first build's: its mount was never moved by hand if it stands at the compensator's tip
    last = list(ob.get("vrp_guess", ob.location))
    if all(abs(a - b) < 1e-5 for a, b in zip(ob.location, last)):
        ob.location = guess
        ob["vrp_guess"] = list(guess)
        return ob, "guessed again"
    return ob, "kept (moved by hand)"


def material(name, rgba):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = rgba
    return m


if os.path.exists(OUT):
    bpy.ops.wm.open_mainfile(filepath=OUT)
    print("adding to", OUT)
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.length_unit = "CENTIMETERS"
grey = material("weapon", (0.55, 0.55, 0.6, 1.0))
pale = material("optional", (0.75, 0.55, 0.55, 1.0))
blue = material("attachment", (0.35, 0.45, 0.7, 1.0))
shapes = read_lua_table(os.path.join(DATA, "weapon_shapes.lua"))
outline = read_lua_table(os.path.join(DATA, "weapon_boxes.lua"))
attach = read_lua_table(os.path.join(DATA, "weapon_attachments.lua"))
files = sorted(glob.glob(os.path.join(SRC, "*.mesh.json")))
atts = bpy.data.collections.get("attachments")
if atts is None:
    atts = bpy.data.collections.new("attachments")
    scene.collection.children.link(atts)

first_mounts = None
for family in FAMILIES:
    coll = bpy.data.collections.get(family)
    if coll is None:
        coll = bpy.data.collections.new(family)
        scene.collection.children.link(coll)
    coll["vrp_kind"] = "weapon"
    optional = child_collection(coll, family + "/optional", hidden=True)
    shown = default_meshes(family)
    body, extra = [], []
    for path in files:
        name = os.path.basename(path).split(".")[0].lower()
        if not name.startswith(family + "__") or SKIP.search(name):
            continue
        ob = bpy.data.objects.get(name)
        if ob is None:
            verts, faces = mesh_of(path)
            if not verts:
                continue
            ob = mesh_object(name, verts, faces, coll, grey)
        is_optional = bool(OPTIONAL_ALWAYS.search(name)) or (shown is not None and name not in shown)
        link(ob, optional if is_optional else coll)
        ob.data.materials[0] = pale if is_optional else grey
        (extra if is_optional else body).append(name)
    if not body:
        print("NO MESHES for", family)
        continue
    pts = [v.co for name in body for v in bpy.data.objects[name].data.vertices]
    # MUZZLE: the front of what is always there, at the middle of its end
    front = max(p.y for p in pts)
    tip = [p for p in pts if p.y > front - 0.01]
    muzzle, how_m = mount_empty(family + "/muzzle", coll,
                                ((min(p.x for p in tip) + max(p.x for p in tip)) * 0.5, front, (min(p.z for p in tip) + max(p.z for p in tip)) * 0.5), "ARROWS")
    # SCOPE: the top of the rail where there is one, else of the gun, over the middle of the top's length
    top_pts = [v.co for name in extra if "_rail" in name for v in bpy.data.objects[name].data.vertices] or pts
    top = max(p.z for p in top_pts)
    flat = [p for p in top_pts if p.z > top - 0.003]
    scope, how_s = mount_empty(family + "/scope", coll, (0.0, (min(p.y for p in flat) + max(p.y for p in flat)) * 0.5, top), "SINGLE_ARROW")
    if not any(o.name.lower().startswith("box") for o in coll.objects):
        for i, s in enumerate(shapes.get(family) or ([outline[family]] if family in outline else [])):
            box_object("box_%02d" % (i + 1), ((s[0] + s[1]) / 2, (s[2] + s[3]) / 2, (s[4] + s[5]) / 2),
                       ((s[1] - s[0]) / 2, (s[3] - s[2]) / 2, (s[5] - s[4]) / 2), coll)
    print(family, "shown", body, "optional", extra)
    print("   muzzle", how_m, tuple(round(c, 4) for c in muzzle.location), "scope", how_s, tuple(round(c, 4) for c in scope.location))
    if first_mounts is None:
        first_mounts = (muzzle, scope)

# ATTACHMENTS, once each: hung on the first weapon's mounts to be drawn, and the same on every weapon.
if first_mounts:
    groups = {}
    for path in files:
        name = os.path.basename(path).split(".")[0].lower()
        if SKIP.search(name):
            continue
        m = MUZZLE.match(name)
        if m:
            groups.setdefault(("muzzle", m.group(1)), []).append((name, path))
            continue
        m = SCOPE.match(name)
        if m:
            groups.setdefault(("scope", m.group(1)), []).append((name, path))
    kinds = {"muzzle": child_collection(atts, "muzzles"), "scope": child_collection(atts, "scopes")}
    for (slot, key), parts in sorted(groups.items()):
        if bpy.data.collections.get(key) is not None:
            continue
        ac = bpy.data.collections.new(key)
        kinds[slot].children.link(ac)
        ac["vrp_kind"] = "attachment"
        ac["vrp_slot"] = slot
        root = bpy.data.objects.new(key + "/mount", None)
        root.empty_display_type = "PLAIN_AXES"
        root.empty_display_size = 0.01
        ac.objects.link(root)
        root.parent = first_mounts[0] if slot == "muzzle" else first_mounts[1]
        # it sits exactly at the weapon's mount: not to be moved, only the weapon's mount is
        root.lock_location = root.lock_rotation = (True, True, True)
        root.hide_select = True
        lo, hi = [1e9] * 3, [-1e9] * 3
        for name, path in parts:
            verts, faces = mesh_of(path)
            if not verts:
                continue
            mesh_object(name, verts, faces, ac, blue).parent = root
            if name == key + "_01" or len(parts) == 1:
                for v in verts:
                    for i in range(3):
                        lo[i], hi[i] = min(lo[i], v[i]), max(hi[i], v[i])
        a = attach.get(key)
        if slot == "muzzle":
            # today's box: 5 cm across, from the mount as far as the mesh reaches
            reach = a[3] if a else hi[1]
            box_object("box_01", (0.0, reach / 2, 0.0), (0.025, reach / 2, 0.025), ac, parent=root)
        elif lo[0] < hi[0]:
            box_object("box_01", tuple((lo[i] + hi[i]) / 2 for i in range(3)), tuple((hi[i] - lo[i]) / 2 for i in range(3)), ac, parent=root)
        ac.hide_viewport = key not in ("w_att__power_03", "w_att__scope_short_04")
        print("attachment", slot, key, len(parts), "meshes")

for screen in bpy.data.screens:
    for area in screen.areas:
        for space in area.spaces:
            if space.type == "VIEW_3D":
                space.clip_start = 0.001
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("saved", OUT)
