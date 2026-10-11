"""Small parts: the switch bracket, the three button caps, and the stem-cutting jig."""
from __future__ import annotations

from manifold3d import CrossSection, Manifold

import geom as g
from geom import BRACKET, BTN, HB, SHELF_T, SHELF_Z1, SW, WALL, W, box, cyl_y, cyl_z


def switch_centres() -> list[tuple[float, float]]:
    return [(W / 2 + k * BTN["spacing"], BTN["y"]) for k in (-1, 0, 1)]


def bracket() -> Manifold:
    """Shelf with a pocket for each switch, and a plate that screws to the two back-wall posts."""
    z0 = SHELF_Z1 - SHELF_T
    pw = SW["body"] + 2 * SW["pocket_clr"]                  # pocket opening
    hw = pw / 2 + SW["pocket_wall"]
    xs = [c[0] for c in switch_centres()]
    shelf = box(xs[0] - hw - 1.2, xs[-1] + hw + 1.2, BRACKET["y0"], BTN["y"] + hw + 0.8, z0, SHELF_Z1)
    back = box(W / 2 - BRACKET["x"], W / 2 + BRACKET["x"], BRACKET["y0"], BRACKET["y0"] + BRACKET["t"], z0, BRACKET["z1"])
    m = shelf + back
    for x, y in switch_centres():
        m += box(x - hw, x + hw, y - hw, y + hw, SHELF_Z1 - 0.01, SHELF_Z1 + SW["pocket_h"])
        m -= box(x - pw / 2, x + pw / 2, y - pw / 2, y + pw / 2, SHELF_Z1, SHELF_Z1 + SW["pocket_h"] + 1)
        sl, sw = SW["slot"]
        for s in (-1, 1):                                    # leg slots both ways round, so the switch can go in either way
            m -= box(x - sl / 2, x + sl / 2, y + s * SW["slot_off"] - sw / 2, y + s * SW["slot_off"] + sw / 2, z0 - 1, SHELF_Z1 + 1)
            m -= box(x + s * SW["slot_off"] - sw / 2, x + s * SW["slot_off"] + sw / 2, y - sl / 2, y + sl / 2, z0 - 1, SHELF_Z1 + 1)
    for sx in (-1, 1):
        m -= cyl_y(W / 2 + sx * BRACKET["post_x"], BRACKET["post_z"], BRACKET["y0"] - 1, BRACKET["y0"] + BRACKET["t"] + 1, BRACKET["thru"], 24)
    return m


def bracket_to_print(m: Manifold) -> Manifold:
    return m.translate([0, 0, -(SHELF_Z1 - SHELF_T)])


def switches_world() -> Manifold:
    """Stand-ins for the three switches as fitted: body on the shelf, legs below, cut stem above."""
    parts = []
    for x, y in switch_centres():
        h = SW["body"] / 2
        parts.append(box(x - h, x + h, y - h, y + h, SHELF_Z1, SHELF_Z1 + SW["base_h"]))
        for lx in (-6.25, 6.25):
            for ly in (-2.5, 2.5):
                parts.append(box(x + lx - 0.4, x + lx + 0.4, y + ly - 0.4, y + ly + 0.4, SHELF_Z1 - SW["legs"], SHELF_Z1))
        parts.append(cyl_z(x, y, SHELF_Z1 + SW["base_h"], g.STEM_TOP, 3.5, 24))
    return Manifold.compose(parts)


def icon(kind: int) -> CrossSection:
    if kind == 0:
        return CrossSection([[(-1.4, -2.2), (2.4, 0.0), (-1.4, 2.2)]])
    if kind == 1:
        return CrossSection.square([1.3, 4.2], True).translate([-1.2, 0]) + CrossSection.square([1.3, 4.2], True).translate([1.2, 0])
    return CrossSection.square([3.8, 3.8], True)


def cap_world(k: int) -> Manifold:
    """One cap in place on the device: icon on top, flange under the top wall, stem socket below."""
    x, y = switch_centres()[k + 1]
    top, wall_in = HB + BTN["proud"], HB - WALL
    body = cyl_z(x, y, wall_in - 0.01, top, BTN["d"], 64)
    key = box(x - BTN["key_w"] / 2, x + BTN["key_w"] / 2, y - BTN["d"] / 2 - BTN["key_r"], y - BTN["d"] / 2 + 0.6, wall_in - 0.01, top - 0.3)
    flange = cyl_z(x, y, wall_in - BTN["flange_h"], wall_in, BTN["flange"], 64)
    boss_bot = wall_in - BTN["flange_h"] - BTN["boss_h"]
    boss = cyl_z(x, y, boss_bot, wall_in - BTN["flange_h"] + 0.01, BTN["boss"], 48)
    cap = body + key + flange + boss
    cap -= cyl_z(x, y, boss_bot - 0.1, top - 0.6, BTN["socket"], 32)
    cap -= Manifold.cylinder(0.5, BTN["socket"] / 2 + 0.4, BTN["socket"] / 2, 32).translate([x, y, boss_bot - 0.1])   # lead-in
    cap -= icon(k + 1).extrude(1.0).translate([x, y, top - 0.4])
    return cap


def caps_print() -> list[Manifold]:
    """Each cap turned top-down (icon on the bed, socket opening up) and set at the origin."""
    out = []
    for k in (-1, 0, 1):
        x, y = switch_centres()[k + 1]
        out.append(cap_world(k).translate([-x, -y, -(HB + BTN["proud"])]).rotate([180, 0, 0]))
    return out


def jig() -> Manifold:
    """Stem-cutting block: switch body in the pocket, stem out through the hole, cut flush
    with the underside. Every stem comes out at SW['stem_total']."""
    pw = SW["body"] + 0.3
    floor = SW["stem_total"] - SW["base_h"]
    h = floor + SW["base_h"]
    m = box(-9.5, 9.5, -9.5, 9.5, 0, h)
    m -= box(-pw / 2, pw / 2, -pw / 2, pw / 2, floor, h + 1)
    m -= cyl_z(0, 0, -1, floor + 1, 4.6, 32)
    return m
