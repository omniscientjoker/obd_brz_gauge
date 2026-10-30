// V2 removable-carrier enclosure. All dimensions are millimetres.
// X points right, Y up when looking at the REAR of the fitted module.
// Assembly Z points from the front face toward the rear of the enclosure.
$fn = 72;
eps = 0.05;

// Waveshare STEP/drawing dimensions. The STEP file is the source for the
// board and LCD envelope; the display body is Ø48.08, not Ø55.
display_d = 48.08;
display_active_d = 45.68;
// Keep only a small manufacturing-safe reveal around the active display area.
display_opening_d = 46.0;
display_fit_d = 48.60;
gasket_t = 0.5;

// The supplied DWG describes the alternate Touch-LCD module.  CG is the
// cover-glass outside diameter and VA is the active viewing diameter.  The
// PCB envelope and its three mounting holes remain the same as profile A.
profile_a = 0;
profile_b = 1;
profile_common = 2;
b_display_d = 55.00;
b_display_active_d = 46.08;
b_display_opening_d = 46.40; // 0.16 mm radial reveal around VA
b_display_fit_d = 56.00;     // 0.50 mm radial clearance around CG
// STEP envelope values. Confirm assembled FPC compression before final
// printing; the carrier still controls the PCB axial plane.
lcd_body_t = 2.75;
pcb_t = 1.6;
pcb_rear_component_h = 6.0; // conservative component/cable envelope

module_pitch = 75.0;
module_centres = [-module_pitch, 0, module_pitch];
case_w = 231.0;
case_h = 82.0; // includes FDM clearance for universal carrier ears
case_r = case_h/2;
panel_t = 3.0;
// R3 board height budget: the central gauge USB plug reaches Z=25.6 mm.
// The central gauge lead constrains the stack.
// Keep 2.5 mm PCB-to-floor clearance and the 3 mm bottom plate.
// Total shell depth is panel_t + case_depth = 43 mm.
case_depth = 40.0;
wall_t = 2.4;
bottom_t = 3.0;

// Common panel/carrier interface. These three points stay unchanged when
// changing board profiles. They are separate from the board's three holes.
// Common panel interface is pushed outside the 55 mm B cover-glass envelope.
// A and B carriers use the same three-ear coordinates, so the panel is
// interchangeable and no profile-specific panel is required.
carrier_mount_r = 32.5;
// Rear view: independent ears at 9 / 12 / 3 o'clock; USB is at 6 o'clock.
// 75 mm pitch gives adjacent Ø9 mm post roots a 1 mm edge clearance.
carrier_mount_angles = [180, 90, 0];
carrier_mount_xy = [for (a = carrier_mount_angles)
    [carrier_mount_r*cos(a), carrier_mount_r*sin(a)]];
panel_post_h = 2.0;
panel_post_d = 8.0;
panel_post_root_d = 9.0;
// Two tangential gussets spread the post load into the panel while keeping
// the mounting plane and the common LCD relief unchanged.
panel_rib_w = 2.0;
panel_rib_h = 1.8;
panel_rib_l = 5.5; // from an anchor 1 mm inside the post side
panel_collar_od = 58.0;
panel_collar_h = 1.5;

m2_clearance_d = 2.4;
// Supplied M2*3*3 insert: thread M2, length 3 mm, outside diameter 3 mm.
// Pilot diameter is a trial heat-set fit, not the insert outside diameter.
m2_insert_outer_d = 3.0;
m2_insert_length = 3.0;
m2_insert_d = 2.7;
m2_insert_depth = 3.3;
// Main-board fixing holes are plain through holes. The carrier does not
// receive heat-set inserts at these three positions.
board_mount_clearance_d = 2.6;

carrier_od = 54.5;
carrier_id = 50.6;
carrier_h = 5.0;
carrier_tab_d = 7.4;
carrier_arm_w = 6.0;
carrier_pad_d = 7.2;
carrier_pad_t = 3.6;

// B carrier: the larger cover glass is retained by its own ring.  Keep the
// common three-ear interface and all board supports unchanged.
b_carrier_od = 60.0;
b_carrier_id = 56.0;
b_carrier_h = carrier_h;
b_carrier_tab_d = carrier_tab_d;
b_carrier_arm_w = carrier_arm_w;
b_carrier_pad_d = carrier_pad_d;
b_carrier_pad_t = carrier_pad_t;

// Shared C-frame opening. The front face of the panel is never cut here.
carrier_usb_cut_w = 14.0;
carrier_usb_cut_ymin = -39.0;
carrier_usb_cut_ymax = -20.0;
carrier_usb_root_r = 1.0;
carrier_mark_depth = 0.3;
carrier_mark_size = 1.3;

// A STEP USB shell, transformed to the same rear-view coordinates as the
// mounting holes. B drawings show the same orientation; B's detailed USB
// shell envelope still needs physical confirmation.
board_usb_x = -0.184;
board_usb_face_y = -25.662;
board_usb_body_w = 9.58;
board_usb_body_l = 7.53;
board_usb_body_h = 4.16;
board_usb_center_above_rear = 1.17;
usb_plug_w = 12.0;
usb_plug_down = 10.0;
usb_plug_rear = 14.0; // measured behind the PCB rear face
usb_plug_below_axis = 3.5;
usb_plug_clearance = 0.8;
usb_unmate_travel = 6.5;
usb_cable_d = 4.0;

// A single panel can accept both profiles.  The through opening follows the
// larger VA; the rear locating counterbore follows the larger CG.  The A
// carrier still provides the precise support for the smaller module.
common_display_opening_d = max(display_opening_d, b_display_opening_d);
common_display_fit_d = max(display_fit_d, b_display_fit_d);
// The common panel uses a 58/56 mm rear locating ring. Both the A and B
// cover-glass envelopes fit inside the Ø56 mm relief without changing the
// panel; the A/B carrier controls the remaining screen and PCB support.
common_panel_collar_od = panel_collar_od;

// Exact Waveshare 1.85B board hole pattern extracted from the supplied STEP
// and checked against the DXF. Coordinates are in the rear-view convention.
// Coordinates are viewed from the PCB rear, X right and Y up. Hole diameter
// in the STEP is approximately 2.90 mm; the carrier provides clearance holes
// and the actual screw locking method is supplied by the board side.
board_envelope_w = 47.89;
board_envelope_h = 49.70;
board_hole_d = 2.90;
waveshare_185b_holes = [[-15.73, 14.10], [-15.73, -14.90], [21.27, 0.0]];
// The PCB is identical in all three physical positions.  Position does not
// change the carrier geometry; only the A/B screen profile does.
board_holes = waveshare_185b_holes;
// Compatibility alias for older custom entry files. Do not add position-
// specific geometry here.
board_holes_by_module = [board_holes, board_holes, board_holes];
// Visual inspection aids only: not manufacturer PCB outlines.
pcb_proxy_d_by_module = [board_envelope_h, board_envelope_h, board_envelope_h];

// Six shell screws avoid the carrier ears and stay inside the capsule.
shell_xy = [
    [-case_w/2+8, -14], [-case_w/2+8, 14],
    // Pull the diagonal posts down from the curved ends; retain >=1 mm
    // outer material and clearance to the B ring and panel ear gussets.
    [-90, 32],
    [90, -32],
    [case_w/2-8, -14], [case_w/2-8, 14]
];
shell_clearance_d = 3.4;
shell_post_d = 8.2;
shell_outline_margin = 1.0;
// Two short floor gussets brace each shell post. They remain behind the
// declared PCB/component envelope, away from the screen and carrier ears.
shell_rib_w = 2.4;
shell_rib_h = 6.0;
shell_rib_l = 3.0; // keep the existing 5 mm floor cable channel clear
shell_rib_fan_angle = 40;
// Supplied M3*6*4.2 insert: thread M3, length 6 mm, outside diameter 4.2 mm.
// Ø3.9 mm is a trial heat-set pilot; the blind hole opens at the mating face.
m3_insert_outer_d = 4.2;
m3_insert_length = 6.0;
m3_insert_d = 3.9;
m3_insert_depth = 7.0;

// GCT USB4085-GF-A passes through the uninterrupted 2.4 mm side wall.
// 8.94 x 3.16 mm mouth plus 0.4 mm FDM clearance on each side.
usb_w = 9.74;
usb_h = 3.96;
usb_corner_r = 1.8;
usb_face_exposure = 0.2;
usb_component_corridor_half_w = 8.7;
usb_component_corridor_local_ymax = -23.0;
magnet_pocket_d = 56.3;
magnet_pocket_depth = 0.45;

// One central R3 XH power PCB. Rear-view XY: KiCad (x-100, 100-y).
// Components face the gauges. The magnetic mounting recess is independent
// of wireless charging; no receiver carrier or coil reservation remains.
// 2026-10-23 DRC repair: keep capacitor/fuse copper and USB shell anchors.
// Coordinates use the mounting-hole midpoint, not the outline centroid.
// Move the R3 board toward the lower side so J1 sits directly behind the
// lower-wall USB opening.  The board remains 0.6 mm clear of the inner wall.
power_pcb_centre = [0, -10.5];
power_pcb_w = 82.0;
power_pcb_h = 55.8;
power_pcb_ymin = -27.8;
power_pcb_ymax = 28.0;
power_pcb_outline_xy = [[-41,28],[41,28],[41,-27.5],[6,-27.5],
                        [6,-27.8],[-6,-27.8],[-6,-27.5],[-41,-27.5]];
power_pcb_t = 1.2;
power_hole_pitch = 74.0;
power_components_h = 7.2; // low-profile polymer/component target; verify purchased parts
power_backside_h = 2.0; // 1.8 mm MLCC plus 0.2 mm solder stand-off
power_board_holes = [[-power_hole_pitch/2, 0], [power_hole_pitch/2, 0]];
aux_hole_d = 2.4;
aux_hole_keepout_d = 7.0; // no components or copper around each mounting hole
aux_post_h = 2.5;
aux_post_d = 6.4;
aux_post_root_d = 7.0;
// Connector+wire envelopes are bounded requirements, not purchased models.
// XH mated housings/wires must stay inside Y<=37 mm; plug before closing.
power_xh_front_y = 21.1; // local PCB 31.6 plus enclosure centre Y -10.5
power_xh_wire_limit_y = 37.0;
power_xh_envelope_w = 10.0;
power_xh_centres_x = [-21.9, 0.5, 22.9];
power_xh_envelope_ymin = 9.6; // local envelope 20.1 plus enclosure centre Y
power_usb_tongue_edge_y = -38.3;
power_usb_face_local_y = -30.7;
power_usb_face_y = -41.2;
power_usb_protrusion = power_usb_tongue_edge_y-power_usb_face_y; // 2.9 mm
power_usb_total_extent_y = power_usb_face_y;
power_usb_plug_w = 12.0;
power_usb_plug_down = 8.0; // short right-angle PD extension, not a straight plug
power_usb_plug_h = 8.0;
power_usb_tip_w = 8.6;
power_usb_tip_h = 3.0;
power_usb_shell_w = 8.94;
power_usb_shell_l = 9.17;
power_usb_shell_h = 3.46;
power_usb_mouth_h = 3.16;
power_usb_mouth_r = 1.5;
power_usb_axis_above_pcb = 1.87; // stock STEP front mouth: Z=0.29..3.45
power_usb_front_l = 3.0; // conservative front mouth section before taller rear body

// Two separate side guide channels. No rail runs under the central PCB or
// through the lower shell screw/gussets; central leads are routed above PCB.
cable_channel_w = 5.0;
cable_rail_w = 2.0;
cable_rail_h = 2.0;
// Route the two side channels at Y=-24 so the moved shell posts and the
// lower-wall USB/PCB envelope remain clear.
cable_path_points = [[-82, -24], [-50, -24], [50, -24], [82, -24]];

// Ventilation grille through the 3 mm bottom plate. Slots are outside the
// 82 mm PCB envelope and outside the Ø56.3 mm magnet pocket. The two side
// banks leave broad printed ribs, preserving the shell's torsional strength.
vent_slot_w = 3.0;
vent_slot_h = 8.0;
vent_slot_r = 1.0;
vent_centres_x = [-72, -60, 60, 72];
vent_centres_y = [-14, 0, 14];
vent_floor_z = case_depth-bottom_t;

// Derived assembly planes. Check actual LCD-to-PCB spacing against these.
carrier_front_z = panel_t+panel_post_h;
pcb_front_z = carrier_front_z+carrier_h;
board_usb_axis_z = pcb_front_z+pcb_t+board_usb_center_above_rear;
lcd_front_z = panel_t+gasket_t;
inner_floor_z = panel_t+case_depth-bottom_t;
aux_pcb_floor_side_z = case_depth-bottom_t-aux_post_h;
aux_pcb_component_side_z = aux_pcb_floor_side_z-power_pcb_t;
power_usb_axis_z = aux_pcb_component_side_z-power_usb_axis_above_pcb;
usb_z = power_usb_axis_z;
usb_wall_face_y = -case_h/2;
usb_wall_inner_y = usb_wall_face_y+wall_t;
aux_frontmost_z = panel_t+aux_pcb_component_side_z-power_components_h;
aux_module_clearance = aux_frontmost_z-(pcb_front_z+pcb_t+pcb_rear_component_h);
// The central gauge USB lead passes around the left edge of the R3 board.
// Keep its vertical leg outside the board envelope before it rises to the
// floor channel; this is required after reducing the shell depth.
usb_cable_route_x = -50.0;
usb_cable_route_y = -24.0;
usb_cable_route_z = aux_frontmost_z-usb_cable_d/2-usb_plug_clearance-eps;
function outer_wall_clearance(p) = case_h/2 -
    norm([max(abs(p[0])-(case_w-case_h)/2, 0), p[1]]);
function inner_wall_clearance(p) = outer_wall_clearance(p)-wall_t;
// A capsule is convex: checking the four corners contains the complete
// rectangular XY footprint of each triangular floor gusset.
function shell_gusset_xy(p) = [
    for(a=[atan2(-p[1],-p[0])-shell_rib_fan_angle,
           atan2(-p[1],-p[0])+shell_rib_fan_angle])
        for(x=[shell_post_d/2-1,shell_post_d/2-1+shell_rib_l])
            for(y=[-shell_rib_w/2,shell_rib_w/2])
                p+[x*cos(a)-y*sin(a),x*sin(a)+y*cos(a)]
];
shell_post_outer_gap = min([for(p=shell_xy)
    outer_wall_clearance(p)-shell_post_d/2]);
shell_gusset_outer_gap = min([for(p=shell_xy) for(q=shell_gusset_xy(p))
    outer_wall_clearance(q)]);
panel_post_wall_gap = min([for (cx=module_centres) for (p=carrier_mount_xy)
    inner_wall_clearance([cx+p[0],p[1]])-panel_post_root_d/2]);
carrier_ear_wall_gap = min([for (cx=module_centres) for (p=carrier_mount_xy)
    inner_wall_clearance([cx+p[0],p[1]])-carrier_tab_d/2]);
shell_post_ring_gap = min([for (cx=module_centres) for (p=shell_xy)
    norm([p[0]-cx,p[1]])-max(carrier_od,b_carrier_od)/2-shell_post_d/2]);
adjacent_panel_post_gap = min([for(i=[0:1]) for(j=[i+1:2])
    for(p=carrier_mount_xy) for(q=carrier_mount_xy)
        norm([module_centres[i]+p[0]-module_centres[j]-q[0],p[1]-q[1]])
            -panel_post_root_d]);
adjacent_carrier_ear_gap = min([for(i=[0:1]) for(j=[i+1:2])
    for(p=carrier_mount_xy) for(q=carrier_mount_xy)
        norm([module_centres[i]+p[0]-module_centres[j]-q[0],p[1]-q[1]])
            -max(carrier_tab_d,b_carrier_tab_d)]);

assert(display_opening_d > display_active_d && display_opening_d < display_d);
assert(carrier_id >= display_fit_d);
assert(b_display_opening_d > b_display_active_d && b_display_opening_d < b_display_d);
assert(b_carrier_id >= b_display_fit_d);
assert(m2_insert_d > 2.0 && m2_insert_d < m2_insert_outer_d);
assert(m3_insert_d > 3.0 && m3_insert_d < m3_insert_outer_d);
assert(m2_insert_depth >= m2_insert_length+0.2);
assert(m3_insert_depth >= m3_insert_length+0.5);
assert(panel_t+panel_post_h-m2_insert_depth >= 1.5,
       "Insufficient solid panel material below the M2 blind hole");
assert(panel_post_d >= m2_insert_outer_d+4.0);
assert(shell_post_d >= m3_insert_outer_d+4.0);
assert(m3_insert_depth < case_depth-bottom_t);
assert(panel_rib_h > 0 && panel_rib_h < panel_post_h);
assert(inner_floor_z-shell_rib_h > pcb_front_z+pcb_t+pcb_rear_component_h+2,
       "Shell floor gussets enter the rear component/cable envelope");
assert(board_mount_clearance_d > 2.0 && board_mount_clearance_d < board_hole_d);
assert(carrier_front_z+carrier_h-carrier_pad_t > lcd_front_z+lcd_body_t,
       "Board support arms overlap the LCD: measure the axial stack");
assert(inner_floor_z > pcb_front_z+pcb_t+pcb_rear_component_h+2,
       "Insufficient rear component/cable clearance");
assert(bottom_t-magnet_pocket_depth > 2);
assert(panel_post_wall_gap >= 1, "Panel post too close to base inner wall");
assert(carrier_ear_wall_gap >= 1, "Carrier ear too close to base inner wall");
assert(shell_post_ring_gap >= 1, "Shell post too close to carrier ring");
assert(shell_post_outer_gap >= shell_outline_margin,
       "Shell post exceeds the capsule outline or lacks the outer margin");
assert(shell_gusset_outer_gap >= shell_outline_margin,
       "Shell post gusset exceeds the capsule outline or lacks the outer margin");
assert(adjacent_panel_post_gap >= 1-0.001,
       "Adjacent independent panel insert posts overlap or lack 1 mm clearance");
assert(adjacent_carrier_ear_gap >= 1,
       "Adjacent carrier ears overlap or lack 1 mm clearance");
assert(aux_module_clearance >= 2,
       "Power PCB components too close to gauge modules");
assert(aux_post_h >= power_backside_h+0.5,
       "PCB rear MLCC/solder envelope requires at least 0.5 mm floor margin");
assert(bottom_t+aux_post_h-m2_insert_depth >= 1.5,
       "Insufficient floor material below power PCB insert holes");
assert(aux_post_d >= m2_insert_outer_d+3);
assert(aux_post_d+(aux_post_root_d-aux_post_d)*power_backside_h/aux_post_h
       <= aux_hole_keepout_d-0.1,
       "Power PCB post taper enters the rear component keepout boundary");
assert(aux_hole_d > 2.0);
assert(power_pcb_w-power_hole_pitch >= 2*aux_hole_d);
assert(power_pcb_centre[1]+power_pcb_ymin >=
       -(case_h-2*wall_t)/2+0.3-eps,
       "R3 PCB edge enters the lower side-wall inner face");
assert(abs(usb_wall_face_y-power_usb_face_y-usb_face_exposure) < 0.001,
       "USB-C socket exposure does not match the continuous exterior wall");
assert(usb_w >= power_usb_shell_w+0.8-eps && usb_h >= power_usb_mouth_h+0.8-eps);
assert(usb_corner_r > 0 && 2*usb_corner_r < usb_h);
assert(case_depth-usb_z-usb_h/2 >= bottom_t,
       "USB aperture must leave a full-height lower side-wall ligament");
assert(power_usb_tip_w <= usb_w && power_usb_tip_h <= usb_h,
       "USB-C metal tip cannot pass the socket opening");
assert(usb_component_corridor_half_w < 8.955-0.2,
       "USB collar clearance must not omit the adjacent F1 fuse envelope");
assert(min([for (x=vent_centres_x) abs(x)])-vent_slot_w/2 >
       power_pcb_w/2+wall_t,
       "Vent bank enters the power PCB envelope");
assert(vent_slot_w > 2 && vent_slot_h > 5 && vent_slot_r < vent_slot_w/2);
assert(min([for (x=vent_centres_x) for (y=vent_centres_y)
       norm([x,y])])-norm([vent_slot_w,vent_slot_h])/2 > magnet_pocket_d/2+wall_t,
       "Vent grille enters the magnetic ring pocket");
assert(min([for (x=vent_centres_x) for (y=vent_centres_y) for (p=shell_xy)
       norm([x-p[0],y-p[1]])])-norm([vent_slot_w,vent_slot_h])/2 >
       shell_post_d/2+shell_rib_l+wall_t,
       "Vent grille enters a shell post or its gussets");
assert(min(vent_centres_y)-vent_slot_h/2 >
       cable_path_points[0][1]+cable_channel_w/2+cable_rail_w+1,
       "Vent grille enters the floor cable rails");
assert(power_usb_face_y < power_pcb_centre[1]+power_pcb_ymin+5,
       "R3 USB-C face is not at the lower PCB edge");
assert(power_pcb_h/2+wall_t+1 < case_h/2);
assert(power_xh_wire_limit_y+wall_t+1 < case_h/2,
       "XH mating/wire envelope too close to the upper inner wall");
assert(power_usb_face_y > -case_h/2-wall_t &&
       power_usb_face_y-power_usb_plug_down < -case_h/2+1,
       "PD extension plug must exit through the lower-wall opening");
assert(carrier_usb_cut_w >= usb_plug_w+2*usb_plug_clearance);
assert(carrier_usb_cut_ymin < -max(carrier_od,b_carrier_od)/2);
assert(carrier_usb_cut_ymax > -min(carrier_id,b_carrier_id)/2);
assert(carrier_mark_depth > 0 && carrier_mark_depth < 1.0);
assert(usb_plug_rear > pcb_rear_component_h);
assert(usb_plug_w >= board_usb_body_w);
