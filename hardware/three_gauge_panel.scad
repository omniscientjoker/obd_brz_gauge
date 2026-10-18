// Three-gauge 1.85-inch front panel.
// Units: mm. Measure the actual display and PCB before final printing.

$fn = 72;

// ---------- Waveshare ESP32-S3-Touch-LCD-1.85B dimensions ----------
// Values are taken from the supplied mechanical drawing. Verify the actual
// batch before the final print.
screen_outer_d   = 55.0;   // LCD/bezel outer diameter
screen_opening_d = 46.4;   // 45.68 mm visible area + fit clearance
side_module_w    = screen_outer_d;
side_module_h    = screen_outer_d;
main_board_w     = 47.86;  // PCB envelope from the drawing
main_board_h     = 49.69;

// ---------- panel geometry ----------
front_thickness  = 3.0;
screen_gap       = 8.0;
center_pitch     = side_module_w + screen_gap;
outer_margin_x   = 12.0;
base_w           = side_module_w * 3 + screen_gap * 2 + outer_margin_x * 2;
base_h           = max([side_module_h, main_board_h]) + 18.0;
outer_corner_r   = base_h / 2 - 0.5;

// Six M3 panel-to-base screw holes.
enclosure_screw_d = 3.4;
enclosure_points = [
    [-base_w/2 + 8, -12], [0, -base_h/2 + 4], [base_w/2 - 8, -12],
    [-base_w/2 + 8,  12], [0,  base_h/2 - 4], [base_w/2 - 8,  12]
];

// Three M2 screws per display/board module, matching the triangular fixing
// arrangement in the reference device.
device_screw_d      = 2.2;
device_post_d       = 7.0;
device_post_h       = 3.0;
device_root_d       = 9.0;
device_root_h       = 1.5;
device_screw_radius = 22.0;
device_hole_angles  = [90, 210, 330];

assert(device_root_d > device_post_d && device_root_h < device_post_h,
       "Pillar roots must be wider and lower than the board contact faces");

module rounded_prism(w, h, d, r) {
    linear_extrude(height = d)
        offset(r = r)
            square([w - 2*r, h - 2*r], center = true);
}

module ring(outer_d, inner_d, z, h) {
    difference() {
        cylinder(d = outer_d, h = h);
        translate([0, 0, -0.1]) cylinder(d = inner_d, h = h + 0.2);
    }
}

module screen_hole(x) {
    translate([x, 0, -0.2])
        cylinder(d = screen_opening_d, h = front_thickness + 0.4);
}

module device_cutouts(x) {
    for (a = device_hole_angles) {
        translate([x + device_screw_radius * cos(a),
                   device_screw_radius * sin(a), -0.1])
            cylinder(d = device_screw_d, h = front_thickness + 0.2);
    }
}

module device_bosses(x) {
    for (a = device_hole_angles) {
        post_x = x + device_screw_radius * cos(a);
        post_y = device_screw_radius * sin(a);
        difference() {
            union() {
                translate([post_x, post_y, front_thickness])
                    cylinder(d = device_post_d, h = device_post_h);

                // Flared roots overlap the solid panel by 0.2 mm. Clip the
                // extra root material to the outside of the screen aperture.
                difference() {
                    translate([post_x, post_y, front_thickness - 0.2])
                        cylinder(d1 = device_root_d, d2 = device_post_d,
                                 h = device_root_h + 0.2);
                    translate([x, 0, front_thickness - 0.3])
                        cylinder(d = screen_opening_d,
                                 h = device_root_h + 0.4);
                }
            }
            translate([post_x, post_y, front_thickness - 0.3])
                cylinder(d = device_screw_d - 0.25,
                         h = device_post_h + 0.4);
        }
    }
}

module enclosure_cutouts() {
    for (p = enclosure_points) {
        translate([p[0], p[1], -0.1])
            cylinder(d = enclosure_screw_d, h = front_thickness + 0.2);
        // Front counterbore for a flush M3 screw head.
        translate([p[0], p[1], -0.1])
            cylinder(d = 6.2, h = 0.9);
    }
}

module front_panel() {
    difference() {
        rounded_prism(base_w, base_h, front_thickness, outer_corner_r);
        for (x = [-center_pitch, 0, center_pitch]) screen_hole(x);
        for (x = [-center_pitch, 0, center_pitch]) device_cutouts(x);
        enclosure_cutouts();
    }

    // LCD shoulder and rear screw bosses. The three rear screws per module
    // pull the board against the shoulder without loading the glass.
    for (x = [-center_pitch, 0, center_pitch]) {
        translate([x, 0, front_thickness])
            ring(screen_opening_d + 2.0, screen_opening_d, 0, 1.2);
        device_bosses(x);
    }
}

front_panel();
