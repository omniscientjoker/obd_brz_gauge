// Preview only. Set exploded_gap=0 for the fully assembled enclosure.
include <parts.scad>
exploded_gap=12;
show_modules=true;
show_electronics=true;
show_usb=true;
color("#353a40") universal_panel();
for (i=[0:2]) {
    color("#d5a440")
        translate([module_centres[i], 0, carrier_front_z+exploded_gap])
            carrier_shape(i);
    if (show_modules)
        translate([module_centres[i], 0, exploded_gap]) module_reference(i);
    if (show_usb)
        translate([module_centres[i],0,exploded_gap]) {
            color("#448fc7") board_usb_reference();
            color([0.9,0.3,0.2,0.55]) usb_plug_envelope(clearance=0);
            color([0.2,0.4,0.6,0.4]) usb_cable_envelope(clearance=0,index=i);
        }
}
color([0.2, 0.26, 0.32, 0.45])
    translate([0, 0, panel_t+2*exploded_gap]) rear_base();
if (show_electronics)
    translate([0, 0, panel_t+2*exploded_gap]) electronics_reference();
