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

// The triangle lies in X/Z, with its foot on Z=0. Negative Z overlap
// joins the gusset to the supporting plate instead of leaving a contact face.
module triangular_gusset(length, width, height) {
    rotate([90, 0, 0])
        linear_extrude(height=width, center=true)
            polygon([[0, -eps], [length, -eps], [0, height]]);
}

module panel_post_gussets(p) {
    // Stay tangential to the LCD opening; inward radial ribs would reduce
    // the clearance for the B-profile cover glass.
    tangent = atan2(p[1], p[0])+90;
    for (a=[tangent, tangent+180])
        translate([p[0], p[1], panel_t]) rotate([0, 0, a])
            translate([panel_post_d/2-1, 0, 0])
                triangular_gusset(panel_rib_l, panel_rib_w, panel_rib_h);
}

module carrier_usb_opening_2d() {
    translate([-carrier_usb_cut_w/2, carrier_usb_cut_ymin])
        square([carrier_usb_cut_w, carrier_usb_cut_ymax-carrier_usb_cut_ymin]);
}

// Round the outer slot roots before subtracting the bore. Rounding the thin
// finished ring on both sides would erase A's 1.95 mm-wide wall at R1.
// Opening this solid disc only removes material; the slot stays >=14 mm.
module open_ring_2d(od, id) {
    difference() {
        offset(r=carrier_usb_root_r) offset(delta=-carrier_usb_root_r)
            difference() {
                circle(d=od);
                carrier_usb_opening_2d();
            }
        circle(d=id);
    }
}

// Small stroke labels avoid any dependency on installed OpenSCAD fonts.
function label_points(c) =
    c == "U" ? [[0,1],[0,0],[0.7,0],[0.7,1]] :
    c == "P" ? [[0,0],[0,1],[0.7,1],[0.7,0.5],[0,0.5]] :
    c == "S" ? [[0.7,1],[0,1],[0,0.5],[0.7,0.5],[0.7,0],[0,0]] :
    c == "B" ? [[0,0],[0,1],[0.7,1],[0.7,0.5],[0,0.5],
                [0.7,0.5],[0.7,0],[0,0]] : [];

module stroke_label(word) {
    translate([-(len(word)-1+0.7)*carrier_mark_size/2,
               -carrier_mark_size/2])
        for (i=[0:len(word)-1]) let(points=label_points(word[i]))
            for (j=[0:len(points)-2])
                hull() for (k=[j,j+1])
                    translate([(i+points[k][0])*carrier_mark_size,
                               points[k][1]*carrier_mark_size]) circle(d=0.3);
}

module carrier_direction_marks(od, id, thickness) {
    for (item=[[90,"UP"],[-60,"USB"]]) let(a=item[0],r=(od+id)/4)
        translate([r*cos(a),r*sin(a),thickness-carrier_mark_depth])
            rotate([0,0,a < 0 ? a+90 : a-90])
                linear_extrude(height=carrier_mark_depth+eps)
                    stroke_label(item[1]);
}

module universal_panel_profile(profile=profile_a) {
    p_opening_d = profile == profile_b ? b_display_opening_d :
        (profile == profile_common ? common_display_opening_d : display_opening_d);
    p_fit_d = profile == profile_b ? b_display_fit_d :
        (profile == profile_common ? common_display_fit_d : display_fit_d);
    p_collar_od = profile == profile_b ? b_carrier_od :
        (profile == profile_common ? common_panel_collar_od : panel_collar_od);
    difference() {
        union() {
            capsule(case_w, case_h, panel_t);
            for (cx=module_centres) {
                translate([cx, 0, panel_t-eps])
                    linear_extrude(height=panel_collar_h+eps)
                        open_ring_2d(p_collar_od, p_fit_d);
                for (p=carrier_mount_xy) {
                    translate([cx+p[0], p[1], panel_t-eps])
                        cylinder(d1=panel_post_root_d, d2=panel_post_d,
                                 h=panel_post_h+eps);
                    translate([cx, 0, 0]) panel_post_gussets(p);
                }
            }
        }
        for (cx=module_centres) {
            translate([cx, 0, -eps])
                cylinder(d=p_opening_d, h=panel_t+2*eps);
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

module universal_panel() {
    // The production panel is common to both A and B carriers.
    universal_panel_profile(profile_common);
}

module universal_panel_a() {
    // Kept for comparison and for users who need the original A-only ring.
    universal_panel_profile(profile_a);
}

module universal_panel_b() {
    // Alias retained for explicit B-profile assembly previews.
    universal_panel_profile(profile_common);
}

module carrier_shape_profile(index=1, profile=profile_a, thickness=carrier_h,
                             template=false) {
    p_od = profile == profile_b ? b_carrier_od : carrier_od;
    p_id = profile == profile_b ? b_carrier_id : carrier_id;
    p_tab_d = profile == profile_b ? b_carrier_tab_d : carrier_tab_d;
    p_arm_w = profile == profile_b ? b_carrier_arm_w : carrier_arm_w;
    p_pad_d = profile == profile_b ? b_carrier_pad_d : carrier_pad_d;
    p_pad_t = profile == profile_b ? b_carrier_pad_t : carrier_pad_t;
    // `index` is retained only for source compatibility with the old
    // left/centre/right entry files. All positions use the same PCB holes.
    holes=board_holes;
    pad_z=template ? 0 : thickness-p_pad_t;
    pad_h=template ? thickness : p_pad_t;
    difference() {
        union() {
            linear_extrude(height=thickness)
                difference() {
                  union() {
                    open_ring_2d(p_od, p_id);
                    // Clip the ears' roots to the same LCD clearance bore.
                    for (p=carrier_mount_xy)
                        hull() {
                            translate([p[0], p[1]])
                                circle(d=p_tab_d);
                            translate([p[0]*0.88, p[1]*0.88])
                                circle(d=p_tab_d);
                        }
                  }
                  circle(d=p_id);
                }
            // Dedicated board supports occupy only the three mounting pads.
            // The centre stays open for LCD, components, battery and FPC.
            for (p=holes) {
                r=norm(p);
                assert(r > 1 && r < p_id/2);
                hull() {
                    translate([p[0], p[1], pad_z])
                        cylinder(d=p_pad_d, h=pad_h);
                    translate([p[0]/r*(p_od/2-0.8),
                               p[1]/r*(p_od/2-0.8), pad_z])
                        cylinder(d=p_arm_w, h=pad_h);
                }
            }
        }
        for (p=carrier_mount_xy)
            translate([p[0], p[1], -eps])
                cylinder(d=m2_clearance_d, h=thickness+2*eps);
        for (p=holes)
            translate([p[0], p[1], template ? -eps : pad_z-eps])
                cylinder(d=template ? m2_clearance_d : board_mount_clearance_d,
                         h=template ? thickness+2*eps : pad_h+2*eps);
        // Enforce the shared 6 o'clock opening across the complete A/B part,
        // including support/ear unions and the thin calibration templates.
        translate([0,0,-eps]) linear_extrude(height=thickness+2*eps)
            carrier_usb_opening_2d();
        carrier_direction_marks(p_od, p_id, thickness);
    }
}

// All following reference volumes use assembly Z, measured from panel front.
// The installed elbow is entirely outside the USB mating face, then turns
// toward +Z (rear cover). The 0.8 mm envelope includes printing tolerance.
module board_usb_reference() {
    translate([board_usb_x-board_usb_body_w/2,board_usb_face_y,
               board_usb_axis_z-board_usb_body_h/2])
        cube([board_usb_body_w,board_usb_body_l,board_usb_body_h]);
}

module usb_plug_envelope(clearance=usb_plug_clearance, travel=0) {
    zmin=board_usb_axis_z-usb_plug_below_axis;
    zmax=pcb_front_z+pcb_t+usb_plug_rear;
    translate([board_usb_x-usb_plug_w/2-clearance,
               board_usb_face_y-usb_plug_down-clearance-travel,
               zmin-clearance])
        cube([usb_plug_w+2*clearance,usb_plug_down+2*clearance+travel,
              zmax-zmin+2*clearance]);
}

module usb_cable_envelope(clearance=usb_plug_clearance, index=0) {
    // Stop just above the floor rails; wiring then joins the existing open
    // route. This is a straight cable reservation, not a bend-radius claim.
    start_z=pcb_front_z+pcb_t+usb_plug_rear;
    if(index==1) {
        // Divert the centre gauge lead around the left edge of the central
        // R3 board before it rises to the floor channel. Cable shape/bend
        // radius still needs a real sample.
        hull() {
            translate([board_usb_x,board_usb_face_y-usb_plug_down/2,start_z])
                sphere(d=usb_cable_d+2*clearance);
            translate([usb_cable_route_x,usb_cable_route_y,usb_cable_route_z])
                sphere(d=usb_cable_d+2*clearance);
        }
        translate([usb_cable_route_x,usb_cable_route_y,usb_cable_route_z])
            cylinder(d=usb_cable_d+2*clearance,
                     h=max(eps, inner_floor_z-cable_rail_h-clearance-usb_cable_route_z));
    } else
        translate([board_usb_x,board_usb_face_y-usb_plug_down/2,start_z])
            cylinder(d=usb_cable_d+2*clearance,
                     h=inner_floor_z-cable_rail_h-start_z);
}

module carrier_shape(index=1, thickness=carrier_h, template=false) {
    carrier_shape_profile(index, profile_a, thickness, template);
}

module carrier_shape_b(index=1, thickness=b_carrier_h, template=false) {
    carrier_shape_profile(index, profile_b, thickness, template);
}

module carrier_print_profile(index=1, profile=profile_a, h=carrier_h) {
    translate([0, 0, h]) rotate([180, 0, 0])
        carrier_shape_profile(index, profile, thickness=h);
}

module carrier_print(index=1) {
    // PCB contact pads and all ears sit on the bed; no floating arms.
    carrier_print_profile(index, profile_a, carrier_h);
}

module carrier_print_b(index=1) {
    // Same board interface as A, with the B cover-glass ring.
    carrier_print_profile(index, profile_b, b_carrier_h);
}

module usb_cutouts() {
    // One close-fit through aperture in the original continuous side wall.
    // No recessed plug well, internal collar, board slot or thin mask.
    translate([0, usb_wall_face_y-eps, usb_z]) rotate([-90, 0, 0])
        round_slot(usb_w, usb_h, wall_t+2*eps, usb_corner_r);
}

module ventilation_cutouts() {
    for (x=vent_centres_x) for (y=vent_centres_y)
        translate([x, y, vent_floor_z-eps])
            round_slot(vent_slot_w, vent_slot_h, bottom_t+2*eps, vent_slot_r);
}

module shell_post_gussets(p) {
    inward = atan2(-p[1], -p[0]);
    for (a=[inward-shell_rib_fan_angle, inward+shell_rib_fan_angle])
        translate([p[0], p[1], case_depth-bottom_t]) rotate([0, 0, a])
            translate([shell_post_d/2-1, 0, 0]) scale([1, 1, -1])
                triangular_gusset(shell_rib_l, shell_rib_w, shell_rib_h);
}

module cable_path_2d(width) {
    for (i=[0:2:len(cable_path_points)-2])
        hull() {
            translate(cable_path_points[i]) circle(d=width);
            translate(cable_path_points[i+1]) circle(d=width);
        }
}

// The same volume is used by the validation script to check the clear route.
module floor_cable_channel() {
    // Keep the validation volume off the coincident floor/rail faces. The
    // printed channel remains the full 5 mm route between the 2 mm rails.
    translate([0, 0, case_depth-bottom_t-cable_rail_h+eps])
        linear_extrude(height=cable_rail_h-2*eps)
            cable_path_2d(cable_channel_w-2*eps);
}

module floor_cable_rails() {
    difference() {
        translate([0, 0, case_depth-bottom_t-cable_rail_h])
            linear_extrude(height=cable_rail_h+eps)
                difference() {
                    cable_path_2d(cable_channel_w+2*cable_rail_w);
                    cable_path_2d(cable_channel_w);
                }
        // Open rail ends and a USB entry in the outer centre rail.
        for (x=[-90, 82])
            translate([x, -24, case_depth-bottom_t-cable_rail_h-eps])
                cube([8, 12, cable_rail_h+2*eps]);
        translate([-4, -case_h/2, case_depth-bottom_t-cable_rail_h-eps])
            cube([8, case_h/2-24, cable_rail_h+2*eps]);
        // Open central input/lead exits. There is no wireless detour.
        translate([-23, -30, case_depth-bottom_t-cable_rail_h-eps])
            cube([46, 12, cable_rail_h+2*eps]);
    }
}

module aux_pcb_mounts() {
    for (p=power_board_holes) let(c=power_pcb_centre)
        translate([c[0]+p[0], c[1]+p[1], aux_pcb_floor_side_z])
            cylinder(d1=aux_post_d, d2=aux_post_root_d, h=aux_post_h+eps);
}

module aux_pcb_insert_holes() {
    for (p=power_board_holes) let(c=power_pcb_centre)
        translate([c[0]+p[0], c[1]+p[1], aux_pcb_floor_side_z-eps])
            cylinder(d=m2_insert_d, h=m2_insert_depth+eps);
}

module aux_pcb_shape() {
    size=[power_pcb_w,power_pcb_h,power_pcb_t];
    difference() {
        linear_extrude(height=size[2]) polygon(points=power_pcb_outline_xy);
        for (p=power_board_holes)
            translate([p[0], p[1], -eps])
                cylinder(d=aux_hole_d, h=size[2]+2*eps);
    }
}

// Board and component envelopes for the actual R3 outline, not a STEP model.
module aux_board_envelope() {
    c=power_pcb_centre;
    size=[power_pcb_w,power_pcb_h,power_pcb_t];
    cz=aux_pcb_component_side_z;
    ch=power_components_h;
    sh=power_backside_h;
    translate([c[0], c[1], cz]) aux_pcb_shape();
    difference() {
        translate([c[0]-size[0]/2, c[1]+power_pcb_ymin, cz-ch])
            cube([size[0], size[1], ch]);
        // Latest KiCad geometry has only J1 in this narrow corridor.
        // F1 starts at X=8.955 mm and stays outside it. Use J1's real shell
        // height here instead of a fictitious 7.2 mm solid over its notch.
        translate([c[0]-usb_component_corridor_half_w,
                   c[1]+power_pcb_ymin-eps, cz-ch-eps])
            cube([2*usb_component_corridor_half_w,
                  usb_component_corridor_local_ymax-power_pcb_ymin+eps,
                  ch+2*eps]);
    }
    difference() {
        translate([c[0],c[1],aux_pcb_floor_side_z])
            linear_extrude(height=sh) polygon(points=power_pcb_outline_xy);
        for (p=power_board_holes)
            translate([c[0]+p[0], c[1]+p[1], aux_pcb_floor_side_z-eps])
                cylinder(d=aux_hole_keepout_d, h=sh+2*eps);
    }
}

module power_connector_envelope() {
    power_usb_shell();
    // Internal connector/mating lead reservation up to the stated wire-turn
    // limit. The external PD plug is checked separately by the metal-tip
    // sweep so its plastic housing is allowed to remain outside the shell.
    // XH bodies are 3.6 mm past the board; total mated lead target is <=9 mm.
    for(x=power_xh_centres_x)
        translate([x-power_xh_envelope_w/2,power_xh_envelope_ymin,
                   aux_pcb_component_side_z-power_components_h])
            cube([power_xh_envelope_w,power_xh_wire_limit_y-power_xh_envelope_ymin,
                  power_components_h]);
}

module power_usb_shell() {
    translate([0,power_usb_face_y,power_usb_axis_z]) rotate([-90,0,0])
        round_slot(power_usb_shell_w,power_usb_mouth_h,
                   power_usb_front_l,power_usb_mouth_r);
    translate([-power_usb_shell_w/2,power_usb_face_y+power_usb_front_l,
               aux_pcb_component_side_z-3.45])
        cube([power_usb_shell_w,power_usb_shell_l-power_usb_front_l,power_usb_shell_h]);
}

module power_usb_external_sweep() {
    // Full overmould reaches the protruding mating plane outside the wall.
    translate([-power_usb_plug_w/2,power_usb_face_y-25,
               power_usb_axis_z-power_usb_plug_h/2])
        cube([power_usb_plug_w,25,power_usb_plug_h]);
    translate([-power_usb_tip_w/2, power_usb_face_y-25,
               power_usb_axis_z-power_usb_tip_h/2])
        cube([power_usb_tip_w,25,power_usb_tip_h]);
}

module electronics_reference() {
    color("#3a936d")
        translate([power_pcb_centre[0],power_pcb_centre[1],aux_pcb_component_side_z])
            aux_pcb_shape();
    color([0.2,0.25,0.3,0.35]) power_connector_envelope();
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
            for (p=shell_xy) {
                translate([p[0], p[1], 0])
                    cylinder(d=shell_post_d, h=case_depth-bottom_t+eps);
                shell_post_gussets(p);
            }
            aux_pcb_mounts();
            floor_cable_rails();
        }
        for (p=shell_xy)
            translate([p[0], p[1], -eps])
                cylinder(d=m3_insert_d, h=m3_insert_depth+eps);
        aux_pcb_insert_holes();
        translate([0, 0, case_depth-magnet_pocket_depth])
            cylinder(d=magnet_pocket_d, h=magnet_pocket_depth+eps);
        usb_cutouts();
        ventilation_cutouts();
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
                for (p=board_holes)
                    translate([p[0], p[1], -eps])
                        cylinder(d=board_hole_d, h=pcb_t+2*eps);
            }
}

module module_reference_b(index=1) {
    color([0.12, 0.15, 0.18, 0.35])
        translate([0, 0, lcd_front_z]) cylinder(d=b_display_d, h=lcd_body_t);
    color([0.1, 0.5, 0.25, 0.5])
        translate([0, 0, pcb_front_z])
            difference() {
                linear_extrude(height=pcb_t)
                    scale([board_envelope_w/2, board_envelope_h/2])
                        circle(d=2);
                for (p=board_holes)
                    translate([p[0], p[1], -eps])
                        cylinder(d=board_hole_d, h=pcb_t+2*eps);
            }
}
