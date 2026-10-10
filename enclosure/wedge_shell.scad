// Podcast Jukebox — wedge shell, thin-wall look-and-feel prototype.
// Numbers mirror the touch-wedge mockup (concept C). Change them
// there first, then here. Units mm.
//
//   openscad -o out/wedge_shell.stl wedge_shell.scad
//
// Print: back face down (the model is already oriented that way), no supports.

W       = 118;    // width
DEPTH   = 52;     // base depth
BACK_H  = 70;     // vertical back height
FRONT_H = 10;     // front lip height
WALL    = 1.2;    // prototype wall
CORNER  = 4;      // vertical edge radius

SCREEN_W = 76;  SCREEN_H = 51;  BEZEL_TOP = 14;
SPK_D = 50;  SPK_HOLE = 3;  SPK_PITCH = 5;  SPK_FROM_BOTTOM = 8;
KNOB_HOLE = 7.2;  BTN_HOLE = 16.4;  KNOB_CAP = 24;
CTRL_X = [];                               // touch-only (concept C): no knobs or buttons
CTRL_KNOB = [];
USB_W = 12; USB_H = 4;

FACE_LEN = sqrt(DEPTH*DEPTH + (BACK_H-FRONT_H)*(BACK_H-FRONT_H));
ANGLE    = atan2(BACK_H-FRONT_H, DEPTH);   // slope from the table
ROW_Y    = BEZEL_TOP + SCREEN_H + 8 + KNOB_CAP/2;
$fn = 64;

// Wedge profile in the YZ plane (y = depth toward the front, z = up), extruded along x.
module wedge_solid(w, d, hb, hf, r) {
    hull() {
        for (x = [r, w - r]) {
            translate([x, 0, 0]) {
                // back edge (vertical), front edge (short), as rounded columns
                translate([0, r, 0]) cylinder(h = hb, r = r);
                translate([0, d - r, 0]) cylinder(h = hf, r = r);
                // the slope: a thin plate from back-top to front-top
            }
        }
        translate([r, r, hb - 0.01]) cube([w - 2*r, 0.01, 0.01]);
        translate([r, d - r, hf - 0.01]) cube([w - 2*r, 0.01, 0.01]);
    }
}

// Transform that maps the face's 2D coordinates (u across = x, v down the slope from
// the top edge = +y) onto the sloped face, with +z of the child pointing out of it.
module on_face() {
    translate([0, 0, BACK_H])
        rotate([-(90 - ANGLE), 0, 0])   // child +y now runs down the slope toward the front lip
            children();
}

module face_cutouts() {
    // screen window
    on_face() translate([(W - SCREEN_W)/2, BEZEL_TOP, -5])
        cube([SCREEN_W, SCREEN_H, 10]);
    // knobs and buttons
    for (i = [0 : 1 : len(CTRL_X) - 1])
        on_face() translate([CTRL_X[i], ROW_Y, -5])
            cylinder(h = 10, d = CTRL_KNOB[i] ? KNOB_HOLE : BTN_HOLE);
}

module back_cutouts() {
    cz = SPK_FROM_BOTTOM + SPK_D/2;
    // speaker grille: hex-ish grid of holes inside a circle
    for (ix = [-6 : 6], iz = [-6 : 6]) {
        x = W/2 + ix * SPK_PITCH + (iz % 2 == 0 ? 0 : SPK_PITCH/2);
        z = cz + iz * SPK_PITCH * 0.866;
        if (sqrt((x - W/2)*(x - W/2) + (z - cz)*(z - cz)) <= SPK_D/2 - SPK_HOLE/2)
            translate([x, -5, z]) rotate([-90, 0, 0]) cylinder(h = 10, d = SPK_HOLE);
    }
    // USB slot at the bottom of the back
    translate([W/2 - USB_W/2, -5, 0.6]) cube([USB_W, 10, USB_H]);
}

module shell() {
    difference() {
        wedge_solid(W, DEPTH, BACK_H, FRONT_H, CORNER);
        // hollow: same wedge shrunk by the wall, open at the bottom (z < 0)
        translate([WALL, WALL, -1])
            wedge_solid(W - 2*WALL, DEPTH - 2*WALL, BACK_H - WALL + 1, FRONT_H - WALL + 1, max(0.5, CORNER - WALL));
        face_cutouts();
        back_cutouts();
    }
}

// Lay it back-face-down for printing: rotate so the back (y = 0 plane) becomes the bed.
rotate([90, 0, 0]) shell();
