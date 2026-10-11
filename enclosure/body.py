"""Enclosure D body: the wedge with everything that is fixed to it.

Printed lying on its back wall (the wall with the grille on the bed), so the speaker seat,
the screw bosses and the switch-bracket posts all stand straight up, and the face frame is
a 28 degree roof over nothing wider than its own thickness. The front is open where the
face plate goes; the plate hooks in at the top and is screwed from underneath (plate.py).
"""
from __future__ import annotations

import math

import numpy as np
from manifold3d import Manifold

import geom as g
from geom import (
    AMP,
    BRACKET,
    BTN,
    CLR,
    EDGE,
    FOOT_SCREW,
    GRILLE,
    HB,
    LEDGE,
    LEDGE_T,
    PLATE,
    PLINTH,
    RING,
    SEG,
    SPK,
    USB,
    WALL,
    W,
    box,
    cyl_y,
    cyl_z,
    in_face,
    plate_rr,
    rrect,
)

BACK_BED = ((0.0, -1.0, 0.0), (1.0, 0.0, 0.0), (0.0, 0.0, 1.0))   # printed on the back wall: its outward normal and in-plane axes


def outer() -> Manifold:
    return g.rounded_solid(EDGE, bed=BACK_BED)


def inner(clear: float = 0.0) -> Manifold:
    return g.rounded_solid(EDGE - WALL - clear)


def face_frame() -> Manifold:
    """Skin thickened to the plate's depth around the opening, with a lip behind the plate's
    top edge. Nothing deeper than the skin is left below b = 62: under the opening's bottom
    edge the body is a thin roof over the cavity, and anything hung behind it there would
    start in mid-air when the body prints on its back. The top band runs on into the top
    wall for the same reason."""
    t, p = PLATE["t"], PLATE
    frame = plate_rr(RING, -t - LEDGE_T, 0.0)
    frame += box(p["a0"] - RING, p["a1"] + RING, -1.5, p["b0"] + 1.0, -t - LEDGE_T, 0.0)
    frame -= plate_rr(CLR, -t, 1.0)                                        # the pocket the plate sits in
    frame -= rrect(p["a0"] + CLR, p["a1"] - CLR, p["b0"] + LEDGE, p["b1"] + 10.0, 2.5, -t - LEDGE_T - 1, 1.0)
    frame -= box(-1.0, W + 1.0, 62.0, p["b1"] + 10.0, -t - LEDGE_T - 1, -WALL + 0.01)
    return in_face(frame)


def speaker_mount() -> tuple[Manifold, Manifold]:
    """Seat ring plus four corner bosses (added), and their blind screw holes (cut)."""
    cx, cz, top = W / 2, SPK["cz"], WALL + SPK["seat"]
    f = SPK["frame"] / 2
    seat = _slab_xz(cx - f, cx + f, cz - f, cz + f, 2.0, 0.5, top)
    seat -= cyl_y(cx, cz, -1, top + 1, GRILLE["d"] + 2.0)
    h = SPK["holes"] / 2
    bosses = [cyl_y(cx + sx * h, cz + sz * h, 0.5, top, SPK["boss"]) for sx in (-1, 1) for sz in (-1, 1)]
    holes = [cyl_y(cx + sx * h, cz + sz * h, 0.5, top + 0.1, SPK["hole"], 16) for sx in (-1, 1) for sz in (-1, 1)]
    return seat + Manifold.compose(bosses), Manifold.compose(holes)


def _slab_xz(x0, x1, z0, z1, r, y0, y1) -> Manifold:
    """Rounded rectangle in the x-z plane, from y0 to y1."""
    pts = []
    for cx, cz in ((x0 + r, z0 + r), (x1 - r, z0 + r), (x1 - r, z1 - r), (x0 + r, z1 - r)):
        for t in np.linspace(0, 2 * math.pi, 24, endpoint=False):
            for y in (y0, y1):
                pts.append((cx + r * math.cos(t), y, cz + r * math.sin(t)))
    return Manifold.hull_points(np.array(pts))


def grille() -> Manifold:
    holes = []
    rad = GRILLE["d"] / 2 - GRILLE["hole"] / 2
    p = GRILLE["pitch"]
    for row in range(-20, 21):
        for col in range(-20, 21):
            x = col * p + (p / 2 if row % 2 else 0.0)
            z = row * p * 0.8660254
            if math.hypot(x, z) <= rad:
                holes.append(cyl_y(W / 2 + x, SPK["cz"] + z, -1.5, WALL + 1.5, GRILLE["hole"], 12))
    return Manifold.compose(holes)


def bracket_posts() -> tuple[Manifold, Manifold]:
    """The posts sit where the back wall is already curving into the top, so they are
    clipped to the outside surface rather than started inside the wall."""
    posts, holes = [], []
    for sx in (-1, 1):
        x, z = W / 2 + sx * BRACKET["post_x"], BRACKET["post_z"]
        posts.append(cyl_y(x, z, -1.0, BRACKET["y0"], BRACKET["post_d"]) ^ outer())
        holes.append(cyl_y(x, z, 1.5, BRACKET["y0"] + 0.1, BRACKET["hole"], 16))
    return Manifold.compose(posts), Manifold.compose(holes)


def amp_bosses() -> tuple[Manifold, Manifold]:
    top = WALL + AMP["boss_h"]
    xs = [AMP["x"] - AMP["pitch"] / 2, AMP["x"] + AMP["pitch"] / 2]
    return (Manifold.compose([cyl_y(x, AMP["z"], 0.5, top, AMP["boss"]) for x in xs]),
            Manifold.compose([cyl_y(x, AMP["z"], 1.0, top + 0.1, AMP["hole"], 16) for x in xs]))


def button_holes() -> Manifold:
    cuts = []
    for k in (-1, 0, 1):
        x = W / 2 + k * BTN["spacing"]
        cuts.append(cyl_z(x, BTN["y"], HB - 6, HB + 2, BTN["hole"], 64))
        yb = BTN["y"] - BTN["hole"] / 2                               # notch at the back edge of the hole: keeps the icon upright
        cuts.append(box(x - BTN["notch_w"] / 2, x + BTN["notch_w"] / 2, yb - BTN["notch_r"], yb + 0.6, HB - 6, HB + 2))
    return Manifold.compose(cuts)


def usb_slot() -> Manifold:
    r = USB["h"] / 2
    off = USB["len"] / 2 - r
    rods = [Manifold.cylinder(10, r, -1, SEG).rotate([0, 90, 0]).translate([-3, USB["b"] + s, -USB["inset"]])
            for s in (-off, off)]
    return in_face(Manifold.batch_hull(rods))


def floor_screws(y: float) -> Manifold:
    cuts = []
    for x in FOOT_SCREW["a"]:
        cuts.append(cyl_z(x, y, -1.0, g.floor_z() + 1.0, FOOT_SCREW["thru"], 24))
        cuts.append(cyl_z(x, y, -1.0, 2.2, FOOT_SCREW["head"], 32))
    return Manifold.compose(cuts)


def frustum(inset_top: float, inset_bot: float, r: float) -> Manifold:
    def ring(inset, z):
        x0, x1, y0, y1 = inset, W - inset, inset, g.CHIN - inset
        return [(cx + r * math.cos(t), cy + r * math.sin(t), z)
                for cx, cy in ((x0 + r, y0 + r), (x1 - r, y0 + r), (x1 - r, y1 - r), (x0 + r, y1 - r))
                for t in np.linspace(0, 2 * math.pi, 24, endpoint=False)]
    return Manifold.hull_points(np.array(ring(inset_top, PLINTH + 0.6) + ring(inset_bot, 0.0)))


def foot() -> Manifold:
    """Recessed foot ring under the body. Its faces lean 45 degrees: on its back the body
    prints the foot sideways, and the faces that look toward the back wall are overhangs."""
    return frustum(5.0, 8.6, 4.0) - frustum(6.6, 10.2, 2.4)


def build(y_screw: float) -> Manifold:
    o = outer()
    shell = o - inner()
    shell -= in_face(plate_rr(CLR, -8.0, 8.0))                      # the opening the plate fills
    seat, seat_holes = speaker_mount()
    posts, post_holes = bracket_posts()
    amp, amp_holes = amp_bosses()
    shell += face_frame() ^ o
    shell += seat + posts + amp
    shell -= seat_holes + post_holes + amp_holes
    shell -= grille() + button_holes() + usb_slot()
    shell += foot()
    shell -= floor_screws(y_screw)
    return shell


def to_print(m: Manifold) -> Manifold:
    """Lay the back wall on the bed: world (x, y, z) -> (x, -z, y)."""
    return m.rotate([90, 0, 0])
