"""Enclosure D: thin-wall look-and-feel prototype.

Same numbers as docs/enclosure/enclosure-d-mockup.html. One change for printing:
the front lip under the screen is angled back under the body (perpendicular to the
face), so the shell prints face-down on the bed with no supports.

    python3 enclosure/build_d.py            -> enclosure/out/*.stl + enclosure_d_print.3mf

World frame: x = width, y = depth (back = 0, front = +y), z = height. Seen from the front,
the viewer's right is x = 0, so the USB slot sits on the x = 0 side wall.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import numpy as np
import trimesh
from manifold3d import CrossSection, Manifold

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"

# ---------------- numbers (mm) ----------------
W, THETA, L, T = 114.0, 62.0, 68.0, 36.0
LIP, PLINTH = 7.0, 3.0
WALL = 1.2                 # prototype; final part 2.4
EDGE = 4.0                 # radius on every outside edge
R = [2.0, 2.0, 3.0, 3.0, 12.0]   # profile corners: back-bottom, chin-bottom, face-bottom, face-top, back-top
WINDOW = (74.5, 50.0)      # active area 73.44 x 48.96 plus 1 mm (display position not yet measured)
PLATE = {"x0": 5.5, "b0": 4.0, "b1": L - 5.0, "r": 4.0, "depth": 0.6}
GRILLE = {"d": 44.0, "hole": 1.6, "pitch": 3.2, "cz": 31.5}
BTN = {"d": 11.0, "hole": 11.8, "y": 19.0, "spacing": 18.0, "proud": 0.8, "flange": 14.0, "flange_h": 0.8}
USB = {"len": 12.5, "h": 6.5, "b": L / 2, "inset": 9.0}   # 11.5 x 5.5 plus 1 mm
SEG = 32

th = math.radians(THETA)
S, C = math.sin(th), math.cos(th)
HB = LIP + L * S                       # height at the back
D = T + L * C                          # depth at the face's lower edge
CHIN = D - (LIP - PLINTH) * math.tan(th)
PROFILE = [(0.0, PLINTH), (CHIN, PLINTH), (D, LIP), (T, HB), (0.0, HB)]   # (depth, height), CCW

U = np.array([1.0, 0.0, 0.0])                      # across the face
V = np.array([0.0, C, -S])                         # down the slope
N = np.array([0.0, S, C])                          # out of the face
A = np.array([0.0, T, HB])                         # top-front edge of the face
FACE = np.column_stack([U, V, N, A])               # 3x4: face coords (a, b, c) -> world


def corner_centres() -> list[tuple[np.ndarray, float]]:
    """Centre and remaining radius (after the EDGE sphere) of each rounded profile corner."""
    out = []
    pts = [np.array(p) for p in PROFILE]
    for i, p in enumerate(pts):
        a = pts[i - 1] - p
        b = pts[(i + 1) % len(pts)] - p
        a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
        phi = math.acos(float(np.clip(a @ b, -1, 1)))
        r = max(R[i], EDGE)
        bis = (a + b) / np.linalg.norm(a + b)
        out.append((p + bis * (r / math.sin(phi / 2)), r - EDGE))
    return out


def rounded_solid(ball: float) -> Manifold:
    """Profile extruded across the width, every edge rounded: core (+) sphere(ball)."""
    sphere = Manifold.sphere(ball, SEG).to_mesh().vert_properties[:, :3]
    pts = []
    for centre, rho in corner_centres():
        ring = [centre] if rho < 0.05 else [
            centre + rho * np.array([math.cos(t), math.sin(t)]) for t in np.linspace(0, 2 * math.pi, 48, endpoint=False)]
        for q in ring:
            for x in (EDGE, W - EDGE):
                pts.append(sphere + np.array([x, q[0], q[1]]))
    return Manifold.hull_points(np.vstack(pts))


def in_face(m: Manifold) -> Manifold:
    return m.transform(FACE)


def rounded_rect_slab(x0, x1, y0, y1, r, z0, z1) -> Manifold:
    pts = []
    for cx, cy in ((x0 + r, y0 + r), (x1 - r, y0 + r), (x1 - r, y1 - r), (x0 + r, y1 - r)):
        for t in np.linspace(0, 2 * math.pi, 24, endpoint=False):
            for z in (z0, z1):
                pts.append((cx + r * math.cos(t), cy + r * math.sin(t), z))
    return Manifold.hull_points(np.array(pts))


def frustum(inset_top: float, inset_bot: float, r: float) -> Manifold:
    """Rounded-rect frustum under the body: top at z=PLINTH+0.6, bottom at z=0."""
    def ring(inset, z):
        x0, x1, y0, y1 = inset, W - inset, inset, CHIN - inset
        return [(cx + r * math.cos(t), cy + r * math.sin(t), z)
                for cx, cy in ((x0 + r, y0 + r), (x1 - r, y0 + r), (x1 - r, y1 - r), (x0 + r, y1 - r))
                for t in np.linspace(0, 2 * math.pi, 24, endpoint=False)]
    return Manifold.hull_points(np.array(ring(inset_top, PLINTH + 0.6) + ring(inset_bot, 0.0)))


def grille_holes() -> Manifold:
    holes = []
    rad = GRILLE["d"] / 2 - GRILLE["hole"] / 2
    p = GRILLE["pitch"]
    for row in range(-20, 21):
        for col in range(-20, 21):
            x = col * p + (p / 2 if row % 2 else 0.0)
            z = row * p * 0.8660254
            if math.hypot(x, z) <= rad:
                holes.append(Manifold.cylinder(WALL + 3, GRILLE["hole"] / 2, -1, 12)
                             .rotate([-90, 0, 0]).translate([W / 2 + x, -1.5, GRILLE["cz"] + z]))
    return Manifold.compose(holes)


def usb_slot() -> Manifold:
    r = USB["h"] / 2
    off = USB["len"] / 2 - r
    rods = [Manifold.cylinder(10, r, -1, SEG).rotate([0, 90, 0]).translate([-3, USB["b"] + s, -USB["inset"]])
            for s in (-off, off)]
    return in_face(Manifold.batch_hull(rods))


def plate_outline(c0: float, c1: float) -> Manifold:
    return in_face(rounded_rect_slab(PLATE["x0"], W - PLATE["x0"], PLATE["b0"], PLATE["b1"], PLATE["r"], c0, c1))


def window() -> Manifold:
    bc = (PLATE["b0"] + PLATE["b1"]) / 2
    w, h = WINDOW
    return in_face(Manifold.cube([w, h, 12]).translate([(W - w) / 2, bc - h / 2, -6]))


def button_holes() -> Manifold:
    return Manifold.compose([Manifold.cylinder(6, BTN["hole"] / 2, -1, SEG).translate([W / 2 + k * BTN["spacing"], BTN["y"], HB - 4])
                             for k in (-1, 0, 1)])


def shell_and_plate() -> tuple[Manifold, Manifold]:
    outer = rounded_solid(EDGE)
    inner = rounded_solid(EDGE - WALL)
    shell = outer - inner
    shell -= Manifold.cube([W - 12, CHIN - 11, PLINTH + WALL + 1.05]).translate([6, 7, -1])   # open underneath
    shell += frustum(5.0, 8.0, 4.0) - frustum(6.6, 9.6, 2.4)                                  # floating plinth ring
    shell -= plate_outline(-PLATE["depth"], 3.0)
    shell -= window() + button_holes() + grille_holes() + usb_slot()
    plate = plate_outline(-PLATE["depth"], 0.0) - window()
    return shell, plate


def icon(kind: int) -> CrossSection:
    if kind == 0:
        return CrossSection([[(-1.4, -2.2), (2.4, 0.0), (-1.4, 2.2)]])
    if kind == 1:
        return CrossSection.square([1.3, 4.2], True).translate([-1.2, 0]) + CrossSection.square([1.3, 4.2], True).translate([1.2, 0])
    return CrossSection.square([3.8, 3.8], True)


def caps() -> Manifold:
    h = WALL + BTN["proud"]
    parts = []
    for k in range(3):
        cap = Manifold.cylinder(h + BTN["flange_h"], BTN["d"] / 2, -1, 64) + Manifold.cylinder(BTN["flange_h"], BTN["flange"] / 2, -1, 64)
        cap -= icon(k).extrude(1.0).translate([0, 0, h + BTN["flange_h"] - 0.4])        # 0.4 mm engraving
        parts.append(cap.rotate([180, 0, 0]).translate([k * 20.0, 0, h + BTN["flange_h"]]))   # top face on the bed
    return Manifold.compose(parts)


def to_trimesh(m: Manifold) -> trimesh.Trimesh:
    mesh = m.to_mesh()
    return trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts), process=True)


def face_down(m: Manifold) -> Manifold:
    """Lay the face flat on the bed: rotate the face normal onto -z."""
    return m.rotate([THETA + 180.0, 0, 0])


def report(name: str, tm: trimesh.Trimesh) -> None:
    e = tm.extents
    print(f"{name:28s} {e[0]:6.1f} x {e[1]:6.1f} x {e[2]:6.1f} mm  watertight={tm.is_watertight}  vol={tm.volume / 1000:6.1f} cm3")


def main() -> int:
    OUT.mkdir(exist_ok=True)
    shell, plate = shell_and_plate()
    print(f"outer {W:.1f} x {D:.1f} x {HB:.1f} mm, chin at depth {CHIN:.1f}, face {THETA} deg")
    overlap = (shell ^ plate).volume()
    print(f"plate/shell overlap {overlap:.3f} mm3 (should be ~0)")

    to_trimesh(shell).export(OUT / "enclosure_d_upright_preview.stl")

    fs, fp = face_down(shell), face_down(plate)
    zmin = min(fs.bounding_box()[2], fp.bounding_box()[2])
    fs, fp = fs.translate([0, 0, -zmin]), fp.translate([0, 0, -zmin])
    t_shell, t_plate, t_caps = to_trimesh(fs), to_trimesh(fp), to_trimesh(caps())
    for name, tm in (("enclosure_d_shell", t_shell), ("enclosure_d_faceplate", t_plate), ("enclosure_d_caps", t_caps)):
        tm.export(OUT / f"{name}.stl")
        report(name, tm)

    sys.path.insert(0, str(HERE.parents[1] / "enclosure"))
    from bambu3mf import write_bambu_3mf
    info = write_bambu_3mf(OUT / "enclosure_d_print.3mf", "jukebox_enclosure_d",
                           [{"name": "shell", "mesh": t_shell, "extruder": 1},
                            {"name": "black face plate", "mesh": t_plate, "extruder": 2}])
    print("3mf:", info)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
