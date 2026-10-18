include <params.scad>

module capsule(w, h, depth) {
    linear_extrude(height=depth)
        hull() for (x=[-(w-h)/2, (w-h)/2])
            translate([x, 0]) circle(d=h);
}

module round_slot(w, h, depth, r) {
    linear_extrude(height=depth)
        offset(r=r) square([w-2*r, h-2*r], center=true);
}

module universal_panel() {
    difference() {
        union() {
            capsule(case_w, case_h, panel_t);
            for (cx=module_centres) {
                translate([cx, 0, panel_t-eps])
                    difference() {
                        cylinder(d=panel_collar_od, h=panel_collar_h+eps);
                        translate([0, 0, -eps])
                            cylinder(d=display_fit_d, h=panel_collar_h+3*eps);
                    }
                for (p=carrier_mount_xy)
                    translate([cx+p[0], p[1], panel_t-eps])
                        cylinder(d1=panel_post_root_d, d2=panel_post_d,
                                 h=panel_post_h+eps);
            }
        }
        for (cx=module_centres) {
            translate([cx, 0, -eps])
                cylinder(d=display_opening_d, h=panel_t+2*eps);
            for (p=carrier_mount_xy)
                translate([cx+p[0], p[1], carrier_front_z-m2_insert_depth])
                    cylinder(d=m2_insert_d, h=m2_insert_depth+eps);
        }
        for (p=shell_xy) {
            translate([p[0], p[1], -eps])
                cylinder(d=shell_clearance_d, h=panel_t+2*eps);
            // 0.8 mm recess for a low-profile M3 screw head.
            translate([p[0], p[1], -eps]) cylinder(d=6.2, h=0.8+eps);
        }
    }
}

module carrier_shape(index=1, thickness=carrier_h, template=false) {
    holes=board_holes_by_module[index];
    pad_z=template ? 0 : carrier_h-carrier_pad_t;
    pad_h=template ? thickness : carrier_pad_t;
    difference() {
        union() {
            difference() {
                union() {
                    cylinder(d=carrier_od, h=thickness);
                    // Clip the ears' roots to the same LCD clearance bore.
                    for (p=carrier_mount_xy)
                        hull() {
                            translate([p[0], p[1], 0])
                                cylinder(d=carrier_tab_d, h=thickness);
                            translate([p[0]*0.88, p[1]*0.88, 0])
                                cylinder(d=carrier_tab_d, h=thickness);
                        }
                }
                translate([0, 0, -eps]) cylinder(d=carrier_id, h=thickness+2*eps);
            }
            // Dedicated board supports occupy only the three mounting pads.
            // The centre stays open for LCD, components, battery and FPC.
            for (p=holes) {
                r=norm(p);
                assert(r > 1 && r < carrier_id/2);
                hull() {
                    translate([p[0], p[1], pad_z])
                        cylinder(d=carrier_pad_d, h=pad_h);
                    translate([p[0]/r*(carrier_od/2-0.8),
                               p[1]/r*(carrier_od/2-0.8), pad_z])
                        cylinder(d=carrier_arm_w, h=pad_h);
                }
            }
        }
        for (p=carrier_mount_xy)
            translate([p[0], p[1], -eps])
                cylinder(d=m2_clearance_d, h=thickness+2*eps);
        for (p=holes)
            translate([p[0], p[1], template ? -eps : carrier_h-m2_insert_depth])
                cylinder(d=template ? m2_clearance_d : m2_insert_d,
                         h=template ? thickness+2*eps : m2_insert_depth+eps);
    }
}

module carrier_print(index=1) {
    // PCB contact pads and all ears sit on the bed; no floating arms.
    translate([0, 0, carrier_h]) rotate([180, 0, 0]) carrier_shape(index);
}

module usb_cutouts() {
    translate([0, -case_h/2-eps, usb_z]) rotate([-90, 0, 0])
        round_slot(usb_w, usb_h, wall_t+2*eps, usb_corner_r);
    for (x=[-usb_flange_pitch/2, usb_flange_pitch/2])
        translate([x, -case_h/2-eps, usb_z]) rotate([-90, 0, 0])
            cylinder(d=usb_flange_hole_d, h=wall_t+2*eps);
}

module rear_base() {
    difference() {
        union() {
            difference() {
                capsule(case_w, case_h, case_depth);
                // Keep exactly bottom_t material under the cavity.
                translate([0, 0, -eps])
                    capsule(case_w-2*wall_t, case_h-2*wall_t,
                            case_depth-bottom_t+eps);
            }
            // Pillars extend to and overlap the bottom plate.
            for (p=shell_xy)
                translate([p[0], p[1], 0])
                    cylinder(d=shell_post_d, h=case_depth-bottom_t+eps);
            // Raised rails form a real 5 mm cable channel along the floor.
            for (y=[-29.5, -22.5])
                translate([-82, y-1, case_depth-bottom_t-2])
                    cube([164, 2, 2+eps]);
        }
        for (p=shell_xy)
            translate([p[0], p[1], -eps])
                cylinder(d=m3_insert_d, h=m3_insert_depth+eps);
        translate([0, 0, case_depth-magnet_pocket_depth])
            cylinder(d=magnet_pocket_d, h=magnet_pocket_depth+eps);
        usb_cutouts();
    }
}

module base_print() {
    translate([0, 0, case_depth]) rotate([180, 0, 0]) rear_base();
}

module module_reference(index=1) {
    // Inspection aids only; these are not production mechanical models.
    color([0.12, 0.15, 0.18, 0.35])
        translate([0, 0, lcd_front_z]) cylinder(d=display_d, h=lcd_body_t);
    color([0.1, 0.5, 0.25, 0.5])
        translate([0, 0, pcb_front_z])
            difference() {
                // The PCB is a 47.86 x 49.69 mm near-round envelope.
                linear_extrude(height=pcb_t)
                    scale([board_envelope_w/2, board_envelope_h/2])
                        circle(d=2);
                for (p=board_holes_by_module[index])
                    translate([p[0], p[1], -eps])
                        cylinder(d=board_hole_d, h=pcb_t+2*eps);
            }
}
