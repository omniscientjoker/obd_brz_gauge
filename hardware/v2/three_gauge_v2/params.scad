// V2 removable-carrier enclosure. All dimensions are millimetres.
// X points right, Y up when looking at the REAR of the fitted module.
// Assembly Z points from the front face toward the rear of the enclosure.
$fn = 72;
eps = 0.05;

// Waveshare STEP/drawing dimensions. The STEP file is the source for the
// board and LCD envelope; the display body is Ø48.08, not Ø55.
display_d = 48.08;
display_active_d = 45.68;
display_opening_d = 46.4;
display_fit_d = 48.60;
gasket_t = 0.5;
// STEP envelope values. Confirm assembled FPC compression before final
// printing; the carrier still controls the PCB axial plane.
lcd_body_t = 2.75;
pcb_t = 1.6;
pcb_rear_component_h = 6.0; // conservative component/cable envelope

module_pitch = 63.0;
module_centres = [-module_pitch, 0, module_pitch];
case_w = 207.0;
case_h = 82.0; // includes FDM clearance for universal carrier ears
case_r = case_h/2;
panel_t = 3.0;
case_depth = 26.0; // base depth measured from panel mating face
wall_t = 2.4;
bottom_t = 3.0;

// Common panel/carrier interface. These three points stay unchanged when
// changing board profiles. They are separate from the board's three holes.
carrier_mount_r = 29.5;
carrier_mount_angles = [45, 135, 270];
carrier_mount_xy = [for (a = carrier_mount_angles)
    [carrier_mount_r*cos(a), carrier_mount_r*sin(a)]];
panel_post_h = 2.0;
panel_post_d = 8.0;
panel_post_root_d = 9.0;
panel_collar_od = 58.0;
panel_collar_h = 1.5;

m2_clearance_d = 2.4;
// Match the actual M2 heat-set insert before printing.
m2_insert_d = 3.2;
m2_insert_depth = 3.3;

carrier_od = 54.5;
carrier_id = 50.6;
carrier_h = 5.0;
carrier_tab_d = 7.4;
carrier_arm_w = 6.0;
carrier_pad_d = 7.2;
carrier_pad_t = 3.6;

// Exact Waveshare 1.85B board hole pattern extracted from the supplied STEP.
// Coordinates are viewed from the PCB rear, X right and Y up. Hole diameter
// in the STEP is approximately 2.90 mm; M2 screws use the carrier inserts.
board_envelope_w = 47.86;
board_envelope_h = 49.69;
board_hole_d = 2.90;
waveshare_185b_holes = [[-15.73, 14.10], [-15.73, -14.10], [21.27, 0.0]];
board_holes_by_module = [
    waveshare_185b_holes, // left slave
    waveshare_185b_holes, // centre master
    waveshare_185b_holes  // right slave
];
// Visual inspection aids only: not manufacturer PCB outlines.
pcb_proxy_d_by_module = [board_envelope_h, board_envelope_h, board_envelope_h];

// Six shell screws, including two between modules, avoid the carrier ears.
shell_xy = [
    [-case_w/2+8, -14], [-case_w/2+8, 14],
    [-module_pitch/2, case_h/2-4.5],
    [module_pitch/2, -case_h/2+4.5],
    [case_w/2-8, -14], [case_w/2-8, 14]
];
shell_clearance_d = 3.4;
shell_post_d = 8.2;
// Match the actual M3 insert. Bore opens at the base mating face.
m3_insert_d = 4.6;
m3_insert_depth = 4.5;

usb_w = 12.5;
usb_h = 6.5;
usb_corner_r = 1.5;
usb_flange_pitch = 16.0;
usb_flange_hole_d = 2.2;
usb_floor_clearance = 2.75;
usb_z = case_depth-bottom_t-usb_h/2-usb_floor_clearance;
magnet_pocket_d = 56.3;
magnet_pocket_depth = 0.45;

// Derived assembly planes. Check actual LCD-to-PCB spacing against these.
carrier_front_z = panel_t+panel_post_h;
pcb_front_z = carrier_front_z+carrier_h;
lcd_front_z = panel_t+gasket_t;
inner_floor_z = panel_t+case_depth-bottom_t;
function inner_wall_clearance(p) = (case_h-2*wall_t)/2 -
    norm([max(abs(p[0])-(case_w-case_h)/2, 0), p[1]]);
panel_post_wall_gap = min([for (cx=module_centres) for (p=carrier_mount_xy)
    inner_wall_clearance([cx+p[0],p[1]])-panel_post_root_d/2]);
carrier_ear_wall_gap = min([for (cx=module_centres) for (p=carrier_mount_xy)
    inner_wall_clearance([cx+p[0],p[1]])-carrier_tab_d/2]);
shell_post_ring_gap = min([for (cx=module_centres) for (p=shell_xy)
    norm([p[0]-cx,p[1]])-carrier_od/2-shell_post_d/2]);

assert(display_opening_d > display_active_d && display_opening_d < display_d);
assert(carrier_id >= display_fit_d);
assert(m2_insert_depth < carrier_pad_t);
assert(m2_insert_depth < panel_t+panel_post_h);
assert(carrier_front_z+carrier_h-carrier_pad_t > lcd_front_z+lcd_body_t,
       "Board support arms overlap the LCD: measure the axial stack");
assert(inner_floor_z > pcb_front_z+pcb_t+pcb_rear_component_h+2,
       "Insufficient rear component/cable clearance");
assert(bottom_t-magnet_pocket_depth > 2);
assert(panel_post_wall_gap >= 1, "Panel post too close to base inner wall");
assert(carrier_ear_wall_gap >= 1, "Carrier ear too close to base inner wall");
assert(shell_post_ring_gap >= 1, "Shell post too close to carrier ring");
