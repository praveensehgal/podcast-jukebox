"""Write a Bambu Studio project 3MF: one or more objects, each made of N parts with a filament per part.

Structure mirrors files saved by Bambu Studio 1.9 / 2.x:
  3D/3dmodel.model            per object: mesh objects inline + a composite object with <components>
  Metadata/model_settings.config   <object><part extruder=i/></object> + <plate> + <assemble>
No project_settings.config on purpose: Bambu Studio then keeps the user's current printer /
filament presets and only takes geometry + per-part filament from this file. That also means no
slicer setting (supports, retraction, wall order) can travel in this file: set those in the app.
"""
import uuid
import zipfile
from datetime import datetime, timezone
from xml.sax.saxutils import quoteattr

import numpy as np

P_NS = "http://schemas.microsoft.com/3dmanufacturing/production/2015/06"
CORE_NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
BBL_NS = "http://schemas.bambulab.com/package/2021"
IDENTITY = "1 0 0 0 1 0 0 0 1"

CONTENT_TYPES = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
    ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
    ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
    ' <Default Extension="png" ContentType="image/png"/>\n</Types>\n')
RELS = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
    ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" '
    'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')


def _mesh_xml(obj_id, mesh):
    v = "\n".join(f'     <vertex x="{x:.6g}" y="{y:.6g}" z="{z:.6g}"/>' for x, y, z in mesh.vertices)
    t = "\n".join(f'     <triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in mesh.faces)
    return (f'  <object id="{obj_id}" p:UUID="{uuid.uuid4()}" type="model">\n   <mesh>\n'
            f'    <vertices>\n{v}\n    </vertices>\n    <triangles>\n{t}\n    </triangles>\n'
            f'   </mesh>\n  </object>\n')


def _part_xml(part_id, volume_id, part, source, faces):
    return (
        f'    <part id="{part_id}" subtype="normal_part">\n'
        f'      <metadata key="name" value={quoteattr(part["name"])}/>\n'
        f'      <metadata key="extruder" value="{int(part["extruder"])}"/>\n'
        f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n'
        f'      <metadata key="source_file" value={quoteattr(source + ".obj")}/>\n'
        f'      <metadata key="source_object_id" value="0"/>\n'
        f'      <metadata key="source_volume_id" value="{volume_id}"/>\n'
        f'      <mesh_stat face_count="{faces}" edges_fixed="0" degenerate_facets="0" '
        f'facets_removed="0" facets_reversed="0" backwards_edges="0"/>\n'
        f'    </part>\n')


def _object_xml(obj, next_id, bed_center):
    """One object: its mesh resources, composite resource, build item and settings blocks."""
    parts = obj["parts"]
    allv = np.vstack([p["mesh"].vertices for p in parts])
    lo, hi = allv.min(0), allv.max(0)
    centre = (lo + hi) / 2
    dx, dy = obj.get("offset", (0.0, 0.0))
    place = f"{IDENTITY} {bed_center[0] + dx:.6f} {bed_center[1] + dy:.6f} {(hi[2] - lo[2]) / 2:.6f}"
    comp_id = next_id + len(parts)

    meshes, comps, cfg = [], [], []
    for k, p in enumerate(parts):
        m = p["mesh"].copy()
        m.apply_translation(-centre)
        meshes.append(_mesh_xml(next_id + k, m))
        comps.append(f'    <component objectid="{next_id + k}" p:UUID="{uuid.uuid4()}" transform="{IDENTITY} 0 0 0"/>')
        cfg.append(_part_xml(next_id + k, k, p, obj["name"], len(m.faces)))

    return {
        "id": comp_id,
        "height": float(hi[2] - lo[2]),
        "place": place,
        "resources": "".join(meshes) + (
            f'  <object id="{comp_id}" p:UUID="{uuid.uuid4()}" type="model">\n   <components>\n'
            + "\n".join(comps) + '\n   </components>\n  </object>\n'),
        "item": f'  <item objectid="{comp_id}" p:UUID="{uuid.uuid4()}" transform="{place}" printable="1"/>\n',
        "settings": (
            f'  <object id="{comp_id}">\n    <metadata key="name" value={quoteattr(obj["name"])}/>\n'
            f'    <metadata key="extruder" value="{min(int(p["extruder"]) for p in parts)}"/>\n'
            + "".join(cfg) + '  </object>\n'),
    }


def write_bambu_3mf(path, title, objects, bed_center=(165.0, 160.0)):
    """objects: list of {name, parts: [{name, mesh (trimesh, mm, Z-up), extruder (1-based)}],
    offset: (dx, dy) from bed_center, optional}.

    The parts of one object must share a coordinate frame: they are centred jointly and stay
    in register. Each object is dropped onto the bed at bed_center + offset.
    """
    built, next_id = [], 1
    for obj in objects:
        built.append(_object_xml(obj, next_id, bed_center))
        next_id = built[-1]["id"] + 1

    model = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<model unit="millimeter" xml:lang="en-US" xmlns="{CORE_NS}" xmlns:BambuStudio="{BBL_NS}" '
        f'xmlns:p="{P_NS}" requiredextensions="p">\n'
        ' <metadata name="Application">BambuStudio-02.06.00.51</metadata>\n'
        ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n'
        f' <metadata name="CreationDate">{datetime.now(timezone.utc).date()}</metadata>\n'
        f' <metadata name="Title">{title}</metadata>\n'
        ' <resources>\n' + "".join(b["resources"] for b in built) + ' </resources>\n'
        f' <build p:UUID="{uuid.uuid4()}">\n' + "".join(b["item"] for b in built) + ' </build>\n</model>\n')

    instances = "".join(
        f'    <model_instance>\n      <metadata key="object_id" value="{b["id"]}"/>\n'
        '      <metadata key="instance_id" value="0"/>\n'
        f'      <metadata key="identify_id" value="{100 + k}"/>\n    </model_instance>\n'
        for k, b in enumerate(built))
    assemble = "".join(
        f'   <assemble_item object_id="{b["id"]}" instance_id="0" transform="{b["place"]}" offset="0 0 0" />\n'
        for b in built)
    settings = (
        '<?xml version="1.0" encoding="UTF-8"?>\n<config>\n' + "".join(b["settings"] for b in built) +
        '  <plate>\n    <metadata key="plater_id" value="1"/>\n    <metadata key="plater_name" value=""/>\n'
        '    <metadata key="locked" value="false"/>\n' + instances + '  </plate>\n'
        '  <assemble>\n' + assemble + '  </assemble>\n</config>\n')

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS)
        z.writestr("3D/3dmodel.model", model)
        z.writestr("Metadata/model_settings.config", settings)
    return {
        "objects": len(built),
        "parts": sum(len(o["parts"]) for o in objects),
        "filaments": sorted({int(p["extruder"]) for o in objects for p in o["parts"]}),
        "height_mm": round(max(b["height"] for b in built), 2),
    }
