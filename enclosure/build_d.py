"""Enclosure D, two-part body: the wedge printed on its back, the face plate printed face
down, the switch bracket, three button caps and a stem-cutting jig.

    python3 enclosure/build_d.py     -> enclosure/out/enclosure_d_*.stl
                                        enclosure_d_body.3mf      body + face plate + bracket, one filament
                                        enclosure_d_buttons.3mf   three caps + jig (print this plate by object)

Every number lives in geom.py (same figures as docs/enclosure/enclosure-d-mockup.html).
The script refuses to write files unless the parts assemble without touching and the
plate can swing in past the rails. WALL = 1.2 is the prototype skin; set 2.4 for the final.
"""
from __future__ import annotations

import math
from pathlib import Path

import numpy as np
import trimesh
from manifold3d import JoinType, Manifold

import body as B
import geom as g
import parts as P
import plate as F
from geom import HB, SPK, WALL, W, box, cyl_y, cyl_z

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"


def speaker_world() -> Manifold:
    y0 = WALL + SPK["seat"]
    f = SPK["frame"] / 2
    cx, cz = W / 2, SPK["cz"]
    flange = box(cx - f, cx + f, y0, y0 + SPK["flange"], cz - f, cz + f)
    y_mag = y0 + SPK["depth"] - SPK["magnet_t"]
    basket = Manifold.cylinder(y_mag - (y0 + SPK["flange"]), SPK["basket0"] / 2, SPK["basket1"] / 2, 48) \
        .rotate([-90, 0, 0]).translate([cx, y0 + SPK["flange"], cz])
    magnet = cyl_y(cx, cz, y_mag, y0 + SPK["depth"], SPK["magnet_d"], 48)
    return flange + basket + magnet


def to_trimesh(m: Manifold) -> trimesh.Trimesh:
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts), process=False)


def printability(m: Manifold, layer: float = 0.2, line: float = 0.42) -> dict:
    """Slice a bed-ready solid; area per layer that reaches more than one line past the
    layer below is what a no-support print lays in mid-air."""
    prev = m.slice(layer / 2)
    out = {"first_layer_islands": len(prev.decompose()), "max_islands": 1, "hanging_mm2": 0.0, "worst_mm2": 0.0, "worst_z": 0.0}
    for z in np.arange(1.5 * layer, m.bounding_box()[5], layer):
        cur = m.slice(z)
        hang = (cur - prev.offset(line, JoinType.Round)).area()
        out["max_islands"] = max(out["max_islands"], len(cur.decompose()))
        out["hanging_mm2"] += hang
        if hang > out["worst_mm2"]:
            out["worst_mm2"], out["worst_z"] = hang, float(z)
        prev = cur
    return {k: round(v, 1) for k, v in out.items()}


def gap(a: Manifold, b: Manifold, reach: float = 20.0) -> float:
    return round(a.min_gap(b, reach), 2)


def swing_check(body: Manifold, plate_asm: Manifold) -> list[tuple[int, float]]:
    """Rotate the plate assembly about the bottom edge of the opening (top edge out of the
    body) and report the overlap with the body at each angle; 0 all the way means it swings in."""
    pivot = g.A + g.PLATE["b1"] * g.V                     # bottom edge of the plate, on the face plane
    out = []
    for deg in (40, 35, 30, 25, 20, 15, 10, 5, 2):
        rot = plate_asm.translate(-pivot).rotate([-deg, 0, 0]).translate(pivot)   # -x rotation swings the top forward, out of the body
        out.append((deg, round((rot ^ body).volume(), 2)))
    return out


def checks(body, plate, bracket, caps, speaker, board, switches) -> list[str]:
    bad = []
    pairs = {"body/plate": (body, plate), "body/bracket": (body, bracket), "body/speaker": (body, speaker),
             "body/board": (body, board), "bracket/speaker": (bracket, speaker), "bracket/board": (bracket, board),
             "plate/board": (plate, board), "speaker/board": (speaker, board), "switches/speaker": (switches, speaker),
             "switches/bracket": (switches, bracket), "switches/body": (switches, body),
             "caps/body": (caps, body), "caps/switches": (caps, switches), "caps/bracket": (caps, bracket),
             "plate/bracket": (plate, bracket), "plate/speaker": (plate, speaker)}
    for name, (a, b) in pairs.items():
        v = (a ^ b).volume()
        flag = "" if v < 0.01 else "   <-- OVERLAP"
        print(f"  {name:18s} overlap {v:8.2f} mm3{flag}")
        if v >= 0.01:
            bad.append(name)
    magnet = cyl_y(W / 2, SPK["cz"], WALL + SPK["seat"] + SPK["depth"] - SPK["magnet_t"], WALL + SPK["seat"] + SPK["depth"], SPK["magnet_d"], 48)
    print(f"  magnet to board       {gap(magnet, board)} mm")
    print(f"  switch legs to magnet {gap(switches, magnet)} mm")
    print(f"  speaker to bracket    {gap(speaker, bracket)} mm")
    print(f"  board to body         {gap(board, body)} mm")
    print(f"  bracket to body       {gap(bracket, body)} mm   (0 = sits on its posts)")
    swing = swing_check(body, plate + board)
    print("  swing-in overlap by angle:", ", ".join(f"{d}:{v}" for d, v in swing))
    if any(v > 0.5 for _, v in swing):
        bad.append("swing-in")
    return bad


def report(name: str, tm: trimesh.Trimesh) -> None:
    e = tm.extents
    print(f"  {name:26s} {e[0]:6.1f} x {e[1]:6.1f} x {e[2]:6.1f} mm  watertight={tm.is_watertight}  vol={tm.volume / 1000:6.1f} cm3")


def main() -> int:
    OUT.mkdir(exist_ok=True)
    cav = B.inner(0.3)
    slab = F.slab_world(cav)
    screw_y = F.screw_y(slab)
    body = B.build(screw_y)
    plate = F.build_world(slab, screw_y)
    bracket = P.bracket()
    caps_w = Manifold.compose([P.cap_world(k) for k in (-1, 0, 1)])
    speaker, board, switches = speaker_world(), F.board_world(), P.switches_world()
    print(f"outer {W:.1f} x {g.D:.1f} x {HB:.1f} mm, wall {WALL}, face {g.THETA} deg, floor screws at y = {screw_y:.1f}")
    print(f"switch shelf top z = {g.SHELF_Z1:.2f}, stems cut to {g.SW['stem_total']} mm, stem tops at z = {g.STEM_TOP:.2f}")
    print("fit checks:")
    bad = checks(body, plate, bracket, caps_w, speaker, board, switches)
    slab_hole = cyl_z(g.FOOT_SCREW["a"][0], screw_y, g.floor_z(), g.floor_z() + 7.5, g.FOOT_SCREW["hole"], 16)
    thread = (slab_hole ^ slab).volume() / (math.pi * (g.FOOT_SCREW["hole"] / 2) ** 2)
    print(f"  floor screw thread length in the slab: {thread:.1f} mm")
    if bad:
        print("NOT WRITING FILES:", ", ".join(bad))
        return 1

    pr = {"enclosure_d_body": B.to_print(body), "enclosure_d_faceplate": F.to_print(plate),
          "enclosure_d_bracket": P.bracket_to_print(bracket), "enclosure_d_jig": P.jig()}
    for k, c in zip("abc", P.caps_print()):
        pr[f"enclosure_d_cap_{k}"] = c
    meshes = {}
    print("parts as printed:")
    for name, m in pr.items():
        lo = m.bounding_box()
        m = m.translate([-lo[0], -lo[1], -lo[2]])
        tm = to_trimesh(m)
        if not (tm.is_watertight and tm.is_winding_consistent and tm.volume > 0):
            raise SystemExit(f"{name}: mesh is not a closed, consistently wound solid")
        tm.export(OUT / f"{name}.stl")
        meshes[name] = tm
        report(name, tm)
    print("  body on its back, no supports:", printability(pr["enclosure_d_body"].translate([0, 0, -pr["enclosure_d_body"].bounding_box()[2]])))
    print("  plate face down, no supports: ", printability(pr["enclosure_d_faceplate"].translate([0, 0, -pr["enclosure_d_faceplate"].bounding_box()[2]])))
    to_trimesh(body + plate + bracket + caps_w).export(OUT / "enclosure_d_assembly_preview.stl")

    from bambu3mf import write_bambu_3mf
    bed = (175.0, 160.0)                                   # H2C: 350 x 320
    body_plate = [{"name": "body", "offset": (-48.0, 0.0), "parts": [{"name": "body", "mesh": meshes["enclosure_d_body"], "extruder": 1}]},
                  {"name": "face plate", "offset": (70.0, 30.0), "parts": [{"name": "face plate", "mesh": meshes["enclosure_d_faceplate"], "extruder": 1}]},
                  {"name": "switch bracket", "offset": (70.0, -40.0), "parts": [{"name": "bracket", "mesh": meshes["enclosure_d_bracket"], "extruder": 1}]}]
    buttons = [{"name": f"cap {k}", "offset": (dx, 0.0), "parts": [{"name": f"cap {k}", "mesh": meshes[f"enclosure_d_cap_{k}"], "extruder": 1}]}
               for k, dx in zip("abc", (-30.0, 0.0, 30.0))]
    buttons.append({"name": "stem jig", "offset": (0.0, 35.0), "parts": [{"name": "jig", "mesh": meshes["enclosure_d_jig"], "extruder": 1}]})
    print("3mf body   :", write_bambu_3mf(OUT / "enclosure_d_body.3mf", "jukebox_enclosure_d_body", body_plate, bed))
    print("3mf buttons:", write_bambu_3mf(OUT / "enclosure_d_buttons.3mf", "jukebox_enclosure_d_buttons", buttons, bed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
