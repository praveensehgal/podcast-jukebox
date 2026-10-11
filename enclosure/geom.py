"""Shared numbers and solid-modelling helpers for enclosure D.

World frame: x = width, y = depth (back wall = 0, front = +y), z = height. Seen from the
front, the viewer's right is x = 0, so the USB slot sits on the x = 0 side wall.
Face frame: a = across the face (same as x), b = down the slope from the top edge,
c = out of the face (negative = inside the body). `in_face` maps face coords to world.
"""
from __future__ import annotations

import math

import numpy as np
from manifold3d import CrossSection, Manifold

# ---------------- outside (mm) ----------------
W, THETA, L, T = 114.0, 62.0, 68.0, 41.0   # width, face angle, face length, top depth
LIP, PLINTH = 7.0, 3.0
EDGE = 4.0                                   # radius on every outside edge
R = [2.0, 2.0, 3.0, 3.0, 12.0]               # profile corners: back-bottom, chin-bottom, face-bottom, face-top, back-top
WALL = 1.2                                   # prototype skin; final part 2.4
SEG = 32
BEVEL = math.sqrt(2.0) - 1.0                 # disc/ball radius ratio that gives a 45 degree start on the bed

th = math.radians(THETA)
S, C = math.sin(th), math.cos(th)
HB = LIP + L * S                             # height at the back
D = T + L * C                                # depth at the face's lower edge
CHIN = D - (LIP - PLINTH) * math.tan(th)     # the lip is perpendicular to the face
PROFILE = [(0.0, PLINTH), (CHIN, PLINTH), (D, LIP), (T, HB), (0.0, HB)]   # (depth, height), CCW

U = np.array([1.0, 0.0, 0.0])
V = np.array([0.0, C, -S])
N = np.array([0.0, S, C])
A = np.array([0.0, T, HB])
FACE = np.column_stack([U, V, N, A])         # 3x4: face (a, b, c) -> world

# ---------------- the parts that go inside (mm) ----------------
CLR = 0.15                                   # plate-to-pocket clearance per side
PLATE = {"a0": 4.0, "a1": W - 4.0, "b0": 3.2, "b1": L - 4.2, "r": 4.0, "t": 2.0}
WINDOW = (74.5, 50.0)                        # active area 73.44 x 48.96 plus 1 mm
LEDGE, LEDGE_T = 1.5, 1.6                    # lip behind the plate's top and bottom edges
RING = 2.5                                   # the skin is thickened this far around the opening
TONGUE = {"a": (W / 2 - 30.0, W / 2 + 30.0), "w": 20.0, "reach": 1.3, "t": 1.7}   # tabs that slide under the skin below the opening
BOARD = {"w": 101.5, "h": 54.9, "glass": 4.2, "pcb": 1.6, "back": 4.0, "hole_in": 3.0, "gap": 0.3}
POST = {"d": 6.5, "hole": 2.5}
FOOT_SCREW = {"a": (W / 2 - 30.0, W / 2 + 30.0), "hw": 48.0, "slab_b0": L - 4.2 - 2.4, "foot_b0": 46.0, "foot_c": -12.1,
              "depth": 22.0, "hole": 2.5, "thru": 3.4, "head": 6.5}   # fin behind the plate's bottom edge + a foot behind the board
SPK = {"frame": 50.0, "flange": 1.5, "basket0": 47.0, "basket1": 38.0, "magnet_d": 40.0, "magnet_t": 13.0,
       "depth": 28.2, "seat": 2.0, "cz": 31.5, "holes": 41.0, "boss": 6.5, "hole": 2.5}
GRILLE = {"d": 44.0, "hole": 1.6, "pitch": 3.2}
BTN = {"d": 11.0, "hole": 11.8, "y": 21.0, "spacing": 18.0, "proud": 0.8, "flange": 14.0, "flange_h": 0.8,
       "boss": 6.5, "boss_h": 1.5, "socket": 3.7, "key_w": 1.4, "key_r": 0.8, "notch_w": 2.0, "notch_r": 1.1}
SW = {"body": 12.0, "base_h": 4.3, "stem_total": 8.5, "stem_full": 15.0, "legs": 3.5, "pocket_clr": 0.2,
      "pocket_wall": 1.2, "pocket_h": 3.5, "slot": (14.0, 3.0), "slot_off": 6.25}
STEM_TOP = HB - 0.8                          # the cut stem ends here, inside the cap, whatever the wall thickness
SHELF_Z1 = STEM_TOP - (SW["stem_total"] - SW["base_h"]) - SW["base_h"]   # top of the switch shelf
SHELF_T = 2.4
BRACKET = {"x": 36.0, "y0": 6.4, "t": 2.0, "z1": 61.5, "post_x": 32.0, "post_z": 59.0, "post_d": 7.0, "hole": 2.5, "thru": 3.4}
AMP = {"x": W / 2 + 40.0, "z": 22.0, "pitch": 15.24, "boss": 5.0, "boss_h": 3.0, "hole": 2.5}
USB = {"len": 12.5, "h": 6.5, "b": L / 2, "inset": 9.0}


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


def rounded_solid(ball: float, bed: tuple | None = None) -> Manifold:
    """Profile extruded across the width, every edge rounded: core (+) sphere(ball).

    bed = (outward normal, u, v) names a plane the solid will be printed on. Under every
    sphere centre less than ball*sqrt(2) above that plane a disc goes on the plane, sized
    so the hull leaves the bed at 45 degrees instead of tangentially: same outline higher
    up, no first-layer overhang. (For a centre at height h the disc radius is
    ball*sqrt(2) - h; the usual bevel disc is the h = ball case.)
    """
    sphere = Manifold.sphere(ball, SEG).to_mesh().vert_properties[:, :3]
    turn = np.linspace(0, 2 * math.pi, 48, endpoint=False)
    pts = []
    for centre, rho in corner_centres():
        ring = [centre] if rho < 0.05 else [centre + rho * np.array([math.cos(t), math.sin(t)]) for t in turn]
        for q in ring:
            for x in (EDGE, W - EDGE):
                at = np.array([x, q[0], q[1]])
                pts.append(sphere + at)
                if bed:
                    n, u, v = (np.asarray(k, dtype=float) for k in bed)
                    h = -(at @ n)                       # height of this centre above the bed plane through the origin
                    r = ball * math.sqrt(2.0) - h
                    if r > 0.05:
                        pts.append(at + n * h + r * (np.outer(np.cos(turn), u) + np.outer(np.sin(turn), v)))
    return Manifold.hull_points(np.vstack(pts))


def in_face(m: Manifold) -> Manifold:
    return m.transform(FACE)


def to_face(p: np.ndarray) -> tuple[float, float, float]:
    """World point -> (a, b, c)."""
    r = np.asarray(p) - A
    return float(r @ U), float(r @ V), float(r @ N)


def rrect(x0, x1, y0, y1, r, z0, z1) -> Manifold:
    """Rounded-rectangle slab, axis-aligned, z from z0 to z1."""
    pts = []
    for cx, cy in ((x0 + r, y0 + r), (x1 - r, y0 + r), (x1 - r, y1 - r), (x0 + r, y1 - r)):
        for t in np.linspace(0, 2 * math.pi, 24, endpoint=False):
            for z in (z0, z1):
                pts.append((cx + r * math.cos(t), cy + r * math.sin(t), z))
    return Manifold.hull_points(np.array(pts))


def box(x0, x1, y0, y1, z0, z1) -> Manifold:
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl_z(x, y, z0, z1, d, seg=SEG) -> Manifold:
    return Manifold.cylinder(z1 - z0, d / 2, -1, seg).translate([x, y, z0])


def cyl_y(x, z, y0, y1, d, seg=SEG) -> Manifold:
    """Cylinder along +y (out of the back wall)."""
    return Manifold.cylinder(y1 - y0, d / 2, -1, seg).rotate([-90, 0, 0]).translate([x, y0, z])


def prism_x(poly_yz, x0, x1) -> Manifold:
    """Convex (y, z) polygon extruded from x0 to x1."""
    return Manifold.hull_points(np.array([(x, y, z) for x in (x0, x1) for y, z in poly_yz]))


def plate_rr(grow: float, c0: float, c1: float) -> Manifold:
    """The plate outline grown by `grow` (negative shrinks), as a slab in face coords."""
    p = PLATE
    return rrect(p["a0"] - grow, p["a1"] + grow, p["b0"] - grow, p["b1"] + grow, max(p["r"] + grow, 0.3), c0, c1)


def floor_z() -> float:
    return PLINTH + WALL


def floor_contact(b: float) -> tuple[float, float]:
    """For a face-b, the c and world y where a line of constant b meets the floor's top."""
    c = (floor_z() - HB + b * S) / C
    return c, T + b * C + c * S


def skin_inside_c(b: float) -> float:
    """c of the skin's inner surface on the face-bottom rounded edge (flat skin further up)."""
    bc, cc, ri = L - EDGE, -EDGE, EDGE - WALL
    if b <= bc:
        return -WALL
    return cc + math.sqrt(max(ri * ri - (b - bc) ** 2, 0.0))


def prism_bc(poly: list[tuple[float, float]], a0: float, a1: float) -> Manifold:
    """A (b, c) polygon (any winding, may be concave) extruded across a0..a1 in face coords."""
    cs = CrossSection([poly])
    if cs.area() <= 0:
        cs = CrossSection([poly[::-1]])
    solid = cs.extrude(a1 - a0).translate([0, 0, a0])                 # (b, c, a) -> (a, b, c)
    return solid.transform(np.array([[0.0, 0.0, 1.0, 0.0], [1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0]]))
