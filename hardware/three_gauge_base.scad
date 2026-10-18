// Three-gauge 1.85-inch rear base / wiring box.
// Units: mm. The matching front panel is in three_gauge_panel.scad.
// Measure the actual display and PCB before final printing.

$fn = 72;

// ---------- Waveshare ESP32-S3-Touch-LCD-1.85B dimensions ----------
// Values are taken from the supplied mechanical drawing. Verify the actual
// batch before the final print.
screen_outer_d   = 55.0;
side_module_w    = screen_outer_d;
side_module_h    = screen_outer_d;
main_board_w     = 47.86;
main_board_h     = 49.69;

// ---------- base geometry ----------
side_wall        = 2.4;
bottom_wall      = 3.0; // reinforced rear plate
front_thickness  = 3.0; // matching panel thickness; used for cable retainers
rear_depth       = 23.0;
screen_gap       = 8.0;
outer_margin_x   = 12.0;
base_w           = side_module_w * 3 + screen_gap * 2 + outer_margin_x * 2;
base_h           = max([side_module_h, main_board_h]) + 18.0;
outer_corner_r   = base_h / 2 - 0.5;

// Six M3 base-to-panel fixing pillars. Their XY coordinates must match the
// enclosure_points in three_gauge_panel.scad.
enclosure_screw_d = 3.4;
enclosure_post_d  = 7.5;
enclosure_post_h  = 6.0;
enclosure_points = [
    [-base_w/2 + 8, -12], [0, -base_h/2 + 4], [base_w/2 - 8, -12],
    [-base_w/2 + 8,  12], [0,  base_h/2 - 4], [base_w/2 - 8,  12]
];

// ---------- wiring ----------
wire_channel_w   = 5.0;
wire_channel_h   = 3.0;

// Panel-mount USB-C female connector. usb_cutout_z is measured from the
// panel mating edge (z=0), placing the connector slightly below centre.
usb_cutout_w        = 12.5;
usb_cutout_h        = 6.5;
usb_cutout_z        = 8.0;
usb_mount_hole_d    = 2.2;
usb_mount_hole_pitch = 16.0;

// External circular pocket for the magnetic ring on the rear plate.
magnet_ring_d           = 56.0;
magnet_ring_t           = 0.4;
magnet_pocket_clearance = 0.3;
magnet_pocket_d         = magnet_ring_d + magnet_pocket_clearance;
magnet_pocket_depth     = magnet_ring_t + 0.05;

module rounded_prism(w, h, d, r) {
    linear_extrude(height = d)
        offset(r = r)
            square([w - 2*r, h - 2*r], center = true);
}

module base_outer() {
    rounded_prism(base_w, base_h, rear_depth, outer_corner_r);
}

module inner_cavity() {
    // The open side mates with the rear of the front panel.
    translate([0, 0, -0.1])
        rounded_prism(base_w - 2*side_wall,
                      base_h - 2*side_wall,
                      rear_depth - bottom_wall + 0.2,
                      max([outer_corner_r - side_wall, 1]));
}

module enclosure_posts() {
    for (p = enclosure_points) {
        translate([p[0], p[1], 0])
            difference() {
                cylinder(d = enclosure_post_d, h = enclosure_post_h);
                translate([0, 0, -0.1])
                    cylinder(d = enclosure_screw_d - 0.5,
                             h = enclosure_post_h + 0.2);
            }
    }
}

module usb_c_port_cutout() {
    translate([0, -base_h/2 - 0.1, usb_cutout_z])
        rotate([-90, 0, 0])
            rounded_prism(usb_cutout_w, usb_cutout_h, side_wall + 0.4, 1.5);

    // Optional flange fixing holes for a typical panel-mount USB-C female
    // socket. The pitch can be changed to match the selected connector.
    for (x = [-usb_mount_hole_pitch/2, usb_mount_hole_pitch/2]) {
        translate([x, -base_h/2 - 0.1, usb_cutout_z])
            rotate([-90, 0, 0])
                cylinder(d = usb_mount_hole_d, h = side_wall + 0.4);
    }
}

module rear_base() {
    difference() {
        base_outer();
        inner_cavity();

        // Cable raceway from the centre power/communication board to both
        // slave boards.
        translate([0, -25, rear_depth - bottom_wall - wire_channel_h])
            cube([base_w - 18, wire_channel_w, wire_channel_h + 0.4], center = true);
        translate([0, 0, rear_depth - bottom_wall - wire_channel_h])
            cube([wire_channel_w, 50, wire_channel_h + 0.4], center = true);

        // Circular external recess for the 56 mm x 0.4 mm magnetic ring.
        // The cut starts at the outside face of the rear plate (z=rear_depth)
        // and leaves the remaining plate material intact.
        translate([0, 0, rear_depth - magnet_pocket_depth])
            cylinder(d = magnet_pocket_d, h = magnet_pocket_depth + 0.1);

        // Lower-centre USB-C female connector opening and flange holes.
        usb_c_port_cutout();
    }

    // These six pillars receive the six M3 screws through the front panel.
    enclosure_posts();

    // Low cable retainers. Use zip ties or adhesive-backed saddles.
    for (x = [-side_module_w - screen_gap, side_module_w + screen_gap]) {
        translate([x, -25, front_thickness + 1])
            cube([10, 2.5, 3], center = true);
    }
}

// Keep the base in its assembly coordinate system. In the slicer, rotate it
// 180 degrees around X so the open side faces upward and the rear wall sits
// on the build plate.
rear_base();
