"""Builds -- or adds to -- a .blend to draw weapon collision boxes by hand, from the game's own meshes. Run inside Blender:

    blender -b --python tools/weapon_blend/make_weapon_blend.py -- <dir of serialized .mesh.json> <dir of .app.json> <out .blend> <family> [<family> ...]
        [--ref-attachments <weapon_custom_boxes.lua>]

e.g. family w_handgun__tsunami_nue. Read back with read_weapon_blend.py.

--ref-attachments: for a second .blend with more weapons. The attachments are shown with the boxes already drawn (in that .lua) and
locked, as vrp_kind "attachment_ref", which read_weapon_blend.py does not write: they stay as drawn in the first .blend.
Of the weapons made, only the first is shown; switch the others on in the Outliner. The text block attachments_on_weapon.py
(Scripting tab, Run Script) hangs the attachments on the weapon whose collection is active.

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
# --ref-attachments <weapon_custom_boxes.lua>: the attachments are only shown, with the boxes already drawn there, and are not read
# back (vrp_kind "attachment_ref"), so a second .blend for more weapons cannot overwrite the attachments drawn in the first.
REF_ATTACH = None
if "--ref-attachments" in argv:
    i = argv.index("--ref-attachments")
    REF_ATTACH = os.path.abspath(argv[i + 1])
    del argv[i:i + 2]
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


_APP_DEFAULTS = None


def app_defaults():
    """{app file stem: meshes of its default appearance}, for every .app.json in APPS (read once)."""
    global _APP_DEFAULTS
    if _APP_DEFAULTS is None:
        _APP_DEFAULTS = {}
        for path in glob.glob(os.path.join(APPS, "*.app.json")):
            try:
                with open(path, encoding="utf-8") as f:
                    root = json.load(f)["Data"]["RootChunk"]
            except (ValueError, KeyError):
                continue
            names = set()
            for ap in root.get("appearances") or []:
                d = ap.get("Data", ap) or {}
                n = str(d.get("name", {}).get("$value", ""))
                if n != "default" and not n.endswith("_default"):
                    continue
                for c in d.get("components") or []:
                    m = c.get("mesh")
                    if isinstance(m, dict):
                        p = str(m.get("DepotPath", {}).get("$value", "")).replace("\\", "/")
                        if p:
                            names.add(p.split("/")[-1].split(".")[0].lower())
            _APP_DEFAULTS[os.path.basename(path)[:-len(".app.json")].lower()] = names
    return _APP_DEFAULTS


def default_meshes(family):
    """The meshes of the weapon's default appearance, by name; None when no appearance holds the weapon.

    The weapon's own app (<family>.app: body, receiver, stock, mags) is the one the game puts together; the part apps
    (__base1_01, __rcv1_01, ...) each hold only their part, and their names vary (__base1, __base_01, kang_tao, submachinegun).
    So: the weapon's own app; else the other whole-weapon apps holding its meshes; else every app whose default holds them."""
    return default_sets(family)[0]


def default_sets(family):
    """(its own meshes in the default appearance or None, the meshes of other weapons it borrows there).

    Some weapons are built on another's parts: the Sidewinder carries the Copperhead's receiver and stock, Igla, Palica and Testera
    the Satara's receiver, the Ashura the Nekomata's. They are skinned meshes with no offset, so they sit in the weapon's frame."""
    apps = app_defaults()
    mine = lambda names: {n for n in names if n.startswith(family + "__")}
    if mine(apps.get(family, ())):
        pick = [family]
    else:
        holders = [stem for stem, names in apps.items() if mine(names)]
        # its own apps first (another weapon's app may borrow a part: Unity's holds Liberty's), whole-weapon apps before part apps
        own = [s for s in holders if s.startswith(family)]
        whole = lambda stems: [s for s in stems if s.count("__") == 1]
        pick = whole(own) or own or whole(holders) or holders
    if not pick:
        return None, set()
    every = set().union(*(apps[s] for s in pick))
    borrowed = set()
    # only from the weapon's own apps: when it is found in another weapon's (Tamayura in the Nue's), that weapon's parts are not its
    if any(s.startswith(family) for s in pick):
        borrowed = {n for n in every if re.match(r"w_[a-z_]+__[a-z0-9_]+__", n) and not n.startswith(family + "__")}
    return mine(every), borrowed


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


def read_custom_attachments(path):
    """weapon_custom_boxes.lua -> {attachment: [[cx, cy, cz, hx, hy, hz, qi, qj, qk, qr], ...]}."""
    out, section, name = {}, False, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if s.startswith("attachments = {"):
                section = True
                continue
            if not section:
                continue
            m = re.match(r"([a-z0-9_]+) = \{$", s)
            if m and m.group(1) != "boxes":
                name = m.group(1)
                out.setdefault(name, [])
                continue
            m = re.match(r"\{([-0-9., ]+)\},$", s)
            if m and name:
                out[name].append([float(x) for x in m.group(1).split(",")])
    return out


def layer_collection(lc, name):
    if lc.collection.name == name:
        return lc
    for child in lc.children:
        found = layer_collection(child, name)
        if found:
            return found
    return None


PREVIEW_TEXT = '''"""Hang the attachments on the weapon whose collection is active in the Outliner (click the collection, then Run Script).

Only a preview: the attachments go on exactly at that weapon's muzzle / scope empties. Move those empties, not the attachments."""
import bpy
coll = bpy.context.view_layer.active_layer_collection.collection
name = coll.name.split("/")[0]
muzzle, scope = bpy.data.objects.get(name + "/muzzle"), bpy.data.objects.get(name + "/scope")
if not (muzzle and scope):
    raise SystemExit("Click a weapon's collection first (it has <weapon>/muzzle and <weapon>/scope).")
n = 0
for c in bpy.data.collections:
    if c.get("vrp_kind") in ("attachment", "attachment_ref"):
        root = bpy.data.objects.get(c.name + "/mount")
        if root:
            root.parent = muzzle if c.get("vrp_slot") == "muzzle" else scope
            root.matrix_parent_inverse.identity()
            n += 1
print("attachments on", name, n)
'''


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
made = []
for family in FAMILIES:
    coll = bpy.data.collections.get(family)
    if coll is None:
        coll = bpy.data.collections.new(family)
        scene.collection.children.link(coll)
        made.append(family)
    coll["vrp_kind"] = "weapon"
    optional = child_collection(coll, family + "/optional", hidden=True)
    shown, borrowed = default_sets(family)
    body, extra = [], []
    # what it borrows from another weapon: its own object (the other weapon may be in the file too), always shown
    tag = "@" + family.split("_")[-1][:8]
    for name in sorted(borrowed):
        if SKIP.search(name):
            continue
        path = os.path.join(SRC, name + ".mesh.json")
        oname = (name + tag)[:63]
        ob = bpy.data.objects.get(oname)
        if ob is None and os.path.exists(path):
            verts, faces = mesh_of(path)
            if verts:
                ob = mesh_object(oname, verts, faces, coll, grey)
                ob["vrp_borrowed_from"] = name
        if ob is not None:
            is_optional = bool(OPTIONAL_ALWAYS.search(name))
            link(ob, optional if is_optional else coll)
            ob.data.materials[0] = pale if is_optional else grey
            (extra if is_optional else body).append(oname)
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
    drawn = read_custom_attachments(REF_ATTACH) if REF_ATTACH else None
    for (slot, key), parts in sorted(groups.items()):
        if bpy.data.collections.get(key) is not None:
            continue
        ac = bpy.data.collections.new(key)
        kinds[slot].children.link(ac)
        ac["vrp_kind"] = "attachment_ref" if drawn is not None else "attachment"
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
        if drawn is not None:
            # shown as drawn in the other .blend, locked: this file does not write attachments
            for i, b in enumerate(drawn.get(key, [])):
                ob = box_object("box_%02d" % (i + 1), tuple(b[0:3]), tuple(b[3:6]), ac, parent=root)
                ob.rotation_quaternion = (b[9], b[6], b[7], b[8])
                ob.color = (0.3, 0.6, 1.0, 1.0)
                ob.hide_select = True
            if key not in drawn:
                print("attachment", key, "has no drawn boxes in", REF_ATTACH)
        elif slot == "muzzle":
            # today's box: 5 cm across, from the mount as far as the mesh reaches
            reach = a[3] if a else hi[1]
            box_object("box_01", (0.0, reach / 2, 0.0), (0.025, reach / 2, 0.025), ac, parent=root)
        elif lo[0] < hi[0]:
            box_object("box_01", tuple((lo[i] + hi[i]) / 2 for i in range(3)), tuple((hi[i] - lo[i]) / 2 for i in range(3)), ac, parent=root)
        ac.hide_viewport = key not in ("w_att__power_03", "w_att__scope_short_04")
        print("attachment", slot, key, len(parts), "meshes")

# many weapons share the one frame: of the weapons made now only the first is shown, the others are switched on in the Outliner (eye)
for family in made[1:]:
    lc = layer_collection(bpy.context.view_layer.layer_collection, family)
    if lc:
        lc.hide_viewport = True
text = bpy.data.texts.get("attachments_on_weapon.py") or bpy.data.texts.new("attachments_on_weapon.py")
text.from_string(PREVIEW_TEXT)

for screen in bpy.data.screens:
    for area in screen.areas:
        for space in area.spaces:
            if space.type == "VIEW_3D":
                space.clip_start = 0.001
bpy.ops.wm.save_as_mainfile(filepath=OUT)
print("saved", OUT)
