"""The lift-out face plate: the black glass outline, printed flat, face down.

Carries the Sunton on four posts behind the window, two tongues that hook under the top
edge of the opening, and a slab at its bottom edge that stands on the floor and takes two
M3 screws from underneath. Built in face coordinates, then placed in the world.
"""
from __future__ import annotations

import numpy as np
from manifold3d import Manifold

import geom as g
from geom import BOARD, CLR, FOOT_SCREW, PLATE, POST, TONGUE, WINDOW, W, box, cyl_z, in_face, plate_rr

A_MID, B_MID = W / 2, (PLATE["b0"] + PLATE["b1"]) / 2


def window_cut() -> Manifold:
    w, h = WINDOW
    return box(A_MID - w / 2, A_MID + w / 2, B_MID - h / 2, B_MID + h / 2, -4.0, 1.0)


def tongues() -> Manifold:
    """Two tabs past the plate's bottom edge that slide under the skin below the opening.
    The plate goes in bottom first, tilted about 30 degrees, and swings flat about this
    edge. Set 0.2 mm clear of the skin's inner surface at the tab's tip."""
    t, b1 = PLATE["t"], PLATE["b1"]
    c0 = g.skin_inside_c(b1 + TONGUE["reach"]) - 0.2
    c1 = c0 - TONGUE["t"]
    parts = []
    for a in TONGUE["a"]:
        hw = TONGUE["w"] / 2
        parts.append(box(a - hw, a + hw, b1 - 1.4, b1, c1, -t + 0.01))                   # root, joined to the plate's back
        r = TONGUE["reach"]                                                           # 45 degree underside: prints face down with no support
        poly = [(b1 - 0.01, c1), (b1 + r, c1), (b1 + r, c0 - r), (b1 - 0.01, c0)]
        parts.append(g.prism_bc(poly, a - hw, a + hw))
    return Manifold.compose(parts)


def board_corners() -> list[tuple[float, float]]:
    return [(A_MID + sx * BOARD["w"] / 2, B_MID + sy * BOARD["h"] / 2) for sx in (-1, 1) for sy in (-1, 1)]


def pcb_front_c() -> float:
    return -PLATE["t"] - BOARD["gap"] - BOARD["glass"]


def board_posts() -> tuple[Manifold, Manifold]:
    """Posts to the PCB's front face at its corner holes, and corner stops around the outline."""
    c_top = pcb_front_c()
    posts, holes, stops = [], [], []
    for (ax, by) in board_corners():
        sx, sy = np.sign(ax - A_MID), np.sign(by - B_MID)
        hx, hy = ax - sx * BOARD["hole_in"], by - sy * BOARD["hole_in"]
        posts.append(cyl_z(hx, hy, c_top, -PLATE["t"] + 0.01, POST["d"]))
        holes.append(cyl_z(hx, hy, c_top - 0.1, c_top + 4.0, POST["hole"], 16))
        ox, oy = ax + sx * 0.25, by + sy * 0.25                                            # just outside the PCB
        stops.append(box(min(ox, ox + sx), max(ox, ox + sx), min(oy, oy - sy * 5), max(oy, oy - sy * 5), c_top - 2.0, -PLATE["t"] + 0.01))
        if sy > 0:   # the lower edge gets a stop too; the top edge would brush the ledge as the plate swings in
            stops.append(box(min(ox, ox - sx * 5), max(ox, ox - sx * 5), min(oy, oy + sy), max(oy, oy + sy), c_top - 2.0, -PLATE["t"] + 0.01))
    return Manifold.compose(posts + stops), Manifold.compose(holes)


def screw_slab() -> Manifold:
    """Fin behind the plate's bottom edge, with a foot behind the board's bottom edge that
    reaches the floor. The foot's underside is a 45 degree slope, so the plate prints face
    down without supports. Clipped to the cavity by slab_world."""
    f = FOOT_SCREW
    b0, b1, t = f["slab_b0"], PLATE["b1"] - 0.2, PLATE["t"]
    poly = [(b0, -t + 0.01), (b1, -t + 0.01), (b1, -f["depth"]), (f["foot_b0"], -f["depth"]), (b0, f["foot_c"])]
    return g.prism_bc(poly, W / 2 - f["hw"], W / 2 + f["hw"])


def slab_world(cavity_clear: Manifold) -> Manifold:
    return in_face(screw_slab()) ^ cavity_clear


def screw_y(slab: Manifold) -> float:
    """Middle of the strip where the slab's foot stands on the floor."""
    lo = slab.slice(g.floor_z() + 0.5).bounds()
    return (lo[1] + lo[3]) / 2


def build_world(slab: Manifold, y_screw: float) -> Manifold:
    """The plate in world coordinates with the clipped slab and its screw holes."""
    face = plate_rr(-CLR, -PLATE["t"], 0.0) - window_cut()
    posts, holes = board_posts()
    face = face + tongues() + posts - holes
    world = in_face(face) + slab
    for x in FOOT_SCREW["a"]:
        world -= cyl_z(x, y_screw, g.floor_z() - 1.0, g.floor_z() + 7.5, FOOT_SCREW["hole"], 16)
    return world


def board_world() -> Manifold:
    """Sunton stand-in for the fit checks: glass + PCB + parts, plus the USB connector lip."""
    c0 = -PLATE["t"] - BOARD["gap"]
    depth = BOARD["glass"] + BOARD["pcb"] + BOARD["back"]
    glass = box(A_MID - 44.0, A_MID + 44.0, B_MID - 27.4, B_MID + 27.4, c0 - BOARD["glass"], c0)
    pcb = box(A_MID - BOARD["w"] / 2, A_MID + BOARD["w"] / 2, B_MID - BOARD["h"] / 2, B_MID + BOARD["h"] / 2,
              c0 - depth, c0 - BOARD["glass"])
    usb = box(A_MID - BOARD["w"] / 2 - 1.5, A_MID - BOARD["w"] / 2 + 6.0, g.USB["b"] - 4.5, g.USB["b"] + 4.5,
              c0 - BOARD["glass"] - BOARD["pcb"] - 3.3, c0 - BOARD["glass"] - BOARD["pcb"])
    return in_face(glass + pcb + usb)


def to_print(world: Manifold) -> Manifold:
    """World -> face down on the bed: (a, b, c) -> (a, -b, -c)."""
    inv = np.linalg.inv(np.vstack([g.FACE, [0, 0, 0, 1]]))[:3]
    flip = np.array([[1.0, 0, 0, 0], [0, -1.0, 0, 0], [0, 0, -1.0, 0]])
    return world.transform(inv).transform(flip)
