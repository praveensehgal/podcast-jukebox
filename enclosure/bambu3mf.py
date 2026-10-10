"""Write a Bambu Studio project 3MF: ONE object made of N parts, each part on its own extruder.

Structure mirrors files saved by Bambu Studio 1.9 / 2.x:
  3D/3dmodel.model            mesh objects 1..N inline + composite object N+1 with <components>
  Metadata/model_settings.config   <object><part extruder=i/></object> + <plate> + <assemble>
No project_settings.config on purpose: Bambu Studio then keeps the user's current printer /
filament presets and only takes geometry + per-part extruder from this file.
"""
import uuid
import zipfile
from datetime import date

import numpy as np

P_NS = "http://schemas.microsoft.com/3dmanufacturing/production/2015/06"
CORE_NS = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"
BBL_NS = "http://schemas.bambulab.com/package/2021"


def _mesh_xml(obj_id, mesh):
    v = "\n".join(f'     <vertex x="{x:.6g}" y="{y:.6g}" z="{z:.6g}"/>' for x, y, z in mesh.vertices)
    t = "\n".join(f'     <triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in mesh.faces)
    return (f'  <object id="{obj_id}" p:UUID="{uuid.uuid4()}" type="model">\n   <mesh>\n'
            f'    <vertices>\n{v}\n    </vertices>\n    <triangles>\n{t}\n    </triangles>\n'
            f'   </mesh>\n  </object>\n')


def write_bambu_3mf(path, name, parts, bed_center=(175.0, 160.0)):
    """parts: list of dicts {name, mesh (trimesh, mm, Z-up), extruder (1-based)}.
    All meshes must share one coordinate frame; they are centred jointly."""
    allv = np.vstack([p["mesh"].vertices for p in parts])
    lo, hi = allv.min(0), allv.max(0)
    centre = (lo + hi) / 2
    height = hi[2] - lo[2]
    comp_id = len(parts) + 1

    objs, comps, part_cfg = [], [], []
    for i, p in enumerate(parts, start=1):
        m = p["mesh"].copy()
        m.apply_translation(-centre)
        objs.append(_mesh_xml(i, m))
        comps.append(f'    <component objectid="{i}" p:UUID="{uuid.uuid4()}" '
                     f'transform="1 0 0 0 1 0 0 0 1 0 0 0"/>')
        part_cfg.append(
            f'    <part id="{i}" subtype="normal_part">\n'
            f'      <metadata key="name" value="{p["name"]}"/>\n'
            f'      <metadata key="extruder" value="{p["extruder"]}"/>\n'
            f'      <metadata key="matrix" value="1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"/>\n'
            f'      <metadata key="source_file" value="{name}.obj"/>\n'
            f'      <metadata key="source_object_id" value="0"/>\n'
            f'      <metadata key="source_volume_id" value="{i - 1}"/>\n'
            f'      <mesh_stat face_count="{len(m.faces)}" edges_fixed="0" degenerate_facets="0" '
            f'facets_removed="0" facets_reversed="0" backwards_edges="0"/>\n'
            f'    </part>\n')

    tx, ty, tz = bed_center[0], bed_center[1], height / 2
    model = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<model unit="millimeter" xml:lang="en-US" xmlns="{CORE_NS}" xmlns:BambuStudio="{BBL_NS}" '
        f'xmlns:p="{P_NS}" requiredextensions="p">\n'
        ' <metadata name="Application">BambuStudio-02.06.00.51</metadata>\n'
        ' <metadata name="BambuStudio:3mfVersion">1</metadata>\n'
        f' <metadata name="CreationDate">{date.today()}</metadata>\n'
        f' <metadata name="Title">{name}</metadata>\n'
        ' <resources>\n' + "".join(objs) +
        f'  <object id="{comp_id}" p:UUID="{uuid.uuid4()}" type="model">\n   <components>\n'
        + "\n".join(comps) + '\n   </components>\n  </object>\n </resources>\n'
        f' <build p:UUID="{uuid.uuid4()}">\n'
        f'  <item objectid="{comp_id}" p:UUID="{uuid.uuid4()}" '
        f'transform="1 0 0 0 1 0 0 0 1 {tx:.6f} {ty:.6f} {tz:.6f}" printable="1"/>\n'
        ' </build>\n</model>\n')

    settings = (
        '<?xml version="1.0" encoding="UTF-8"?>\n<config>\n'
        f'  <object id="{comp_id}">\n    <metadata key="name" value="{name}"/>\n'
        '    <metadata key="extruder" value="1"/>\n' + "".join(part_cfg) + '  </object>\n'
        '  <plate>\n    <metadata key="plater_id" value="1"/>\n    <metadata key="plater_name" value=""/>\n'
        '    <metadata key="locked" value="false"/>\n'
        f'    <model_instance>\n      <metadata key="object_id" value="{comp_id}"/>\n'
        '      <metadata key="instance_id" value="0"/>\n      <metadata key="identify_id" value="100"/>\n'
        '    </model_instance>\n  </plate>\n  <assemble>\n'
        f'   <assemble_item object_id="{comp_id}" instance_id="0" '
        f'transform="1 0 0 0 1 0 0 0 1 {tx:.6f} {ty:.6f} {tz:.6f}" offset="0 0 0" />\n'
        '  </assemble>\n</config>\n')

    content_types = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
        ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
        ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
        ' <Default Extension="png" ContentType="image/png"/>\n</Types>\n')
    rels = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
        ' <Relationship Target="/3D/3dmodel.model" Id="rel-1" '
        'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n</Relationships>\n')

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)
        z.writestr("Metadata/model_settings.config", settings)
    return {"object_id": comp_id, "parts": len(parts), "height_mm": round(float(height), 2)}
