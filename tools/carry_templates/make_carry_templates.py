#!/usr/bin/env python3
"""One entity template per prop mesh, so a carried world prop can be replaced by a spawned entity.

Why: docs/native-hand-physics-build-20261108.md, "Spawn proof result". A spawned entity's body is made from the mesh
it has when it attaches, so the mesh has to be in the template. The way the lobby makes its items
(tools/vr_lobby_assets/make_lobby_scene.py, item_entities): the basketball's entity with the mesh path set.

Template path: vrport\\carry\\<16 hex digits of the mesh's path hash>.ent. Also writes carry_templates.lua, the set of
hashes the script may spawn; spawning a path that does not exist is never tried.

Usage: make_carry_templates.py <physics_meshes.tsv> <archive listing> (all | hash [hash ...])
The tsv has "<file name>.mesh.json<TAB>body count<TAB>simulation types"; the listing is WolvenKit's `archive --list`.
"""
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'vr_lobby_assets'))
import make_lobby_scene as lobby  # noqa: E402

ROOT = HERE.parent.parent
CLI = ROOT.parent / 'tools/wolvenkit-9.1.0/WolvenKit.CLI.exe'
WORK = ROOT / 'build/carry-templates'
STAGE = WORK / 'vrport_carry'
RAW = WORK / 'raw'
OUT = WORK / 'out'
LUA = ROOT / 'mods/cet/CyberpunkVRPort_HandCollision/carry_templates.lua'
DEPOT = 'vrport\\carry\\'
INSTALLED = Path(r'E:\SteamLibrary\steamapps\common\Cyberpunk 2077\red4ext\plugins\cyberpunkvrport\archive\cyberpunkvrport.archive')


# THE COLLISION RULES OF THE LOBBY'S BALL. Left to the mesh's own collider they are World Static or nothing on many props, and a
# replacement let go fell through the floor (2026-11-09): a world prop has them from its node, which a template does not.
def filter_data(handle):
    return {'HandleId': str(handle), 'Data': {
        '$type': 'physicsFilterData', 'customFilterData': None, 'preset': lobby.cname('World Dynamic'),
        'queryFilter': {'$type': 'physicsQueryFilter', 'mask1': '0', 'mask2': '143769604'},
        'simulationFilter': {'$type': 'physicsSimulationFilter', 'mask1': '376836', 'mask2': '5070'}}}


# Too small to see, from the moment it exists: it stands in for the copy on the hand until it is let go.
HIDDEN_SCALE = 0.001


def path_hash(path):
    h = 0xcbf29ce484222325
    for b in path.lower().encode():
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def wanted(tsv, mesh_list, picks):
    """The serialized list is by file name only, so the depot paths come from a WolvenKit archive listing. A name
    two meshes share is left out when building all of them: which one was read cannot be told."""
    paths = {}
    for line in Path(mesh_list).read_text(encoding='utf-8', errors='ignore').splitlines():
        line = line.strip()
        if line.lower().endswith('.mesh'):
            paths.setdefault(line.rsplit('\\', 1)[-1].lower(), set()).add(line)
    rows, shared = [], 0
    for line in Path(tsv).read_text(encoding='utf-8').splitlines():
        name, count, kinds = line.split('\t')
        name = name[:-5] if name.endswith('.json') else name
        if count != '1' or kinds != 'Dynamic':
            continue
        found = paths.get(name.lower(), set())
        if len(found) > 1 and picks == ['all']:
            shared += 1
            continue
        for path in found:
            h = '%016x' % path_hash(path)
            if picks == ['all'] or h in picks:
                rows.append((h, path))
    print('left out for a shared name', shared)
    return rows


def main():
    tsv, mesh_list, picks = sys.argv[1], sys.argv[2], sys.argv[3:]
    rows = wanted(tsv, mesh_list, picks)
    print('templates', len(rows))
    if not rows:
        raise SystemExit('nothing to make')
    if WORK.exists():
        shutil.rmtree(WORK)
    (RAW / 'vrport/carry').mkdir(parents=True)
    tmp = WORK / 'template'
    tmp.mkdir()
    # the lobby's build folder may be gone: the template is in the installed archive as well
    source = Path(lobby.TEMPLATE_ITEM_ENT)
    if not source.exists():
        out = WORK / 'installed'
        lobby.run(CLI, 'unbundle', INSTALLED, '-o', out, '-r', r'vrbasketball.vr_basketball\.ent$')
        source = next(out.rglob('vr_basketball.ent'))
    shutil.copyfile(source, tmp / 'item.ent')
    lobby.run(CLI, 'convert', 'serialize', tmp, '-o', tmp, '-w', '*.ent')
    template = (tmp / 'item.ent.json').read_text(encoding='utf-8-sig')
    for h, mesh in rows:
        doc = json.loads(template)
        doc['Header']['ArchiveFileName'] = ''
        root = doc['Data']['RootChunk']
        count = 0
        for c in [root['components'][0]] + [c for c in root['compiledData']['Data']['Chunks']
                                            if c.get('$type') == 'entPhysicalMeshComponent']:
            c['mesh']['DepotPath'] = lobby.depot(mesh)
            c['meshAppearance'] = lobby.cname('default')
            c['name'] = lobby.cname('item')
            c['filterDataSource'] = 'Parent'
            c['filterData'] = filter_data(9000 + count)
            c['visualScale'] = {'$type': 'Vector3', 'X': HIDDEN_SCALE, 'Y': HIDDEN_SCALE, 'Z': HIDDEN_SCALE}
            count += 1
        if count < 2:
            raise SystemExit('the template has no compiled component: ' + mesh)
        # only its own mesh: the template's list names what it was made from (see make_lobby_scene.item_entities)
        root['inplaceResources'] = []
        root['resolvedDependencies'] = [{'DepotPath': lobby.depot(mesh), 'Flags': 'Default'}]
        (RAW / 'vrport/carry' / (h + '.ent.json')).write_text(json.dumps(doc, ensure_ascii=False, indent=1) + '\n',
                                                              encoding='utf-8', newline='\n')
    (STAGE / 'vrport/carry').mkdir(parents=True)
    lobby.run(CLI, 'convert', 'deserialize', RAW / 'vrport/carry', '-o', STAGE / 'vrport/carry')
    OUT.mkdir()
    lobby.run(CLI, 'pack', STAGE, '-o', OUT)
    archive = OUT / 'vrport_carry.archive'
    print('archive', archive, archive.stat().st_size)
    lines = ['-- Generated by tools/carry_templates/make_carry_templates.py: the prop meshes with a template, by path hash.',
             'return {']
    lines += ["  ['%s'] = true," % h for h, _ in sorted(rows)]
    lines += ['}', '']
    LUA.write_text('\n'.join(lines), encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
