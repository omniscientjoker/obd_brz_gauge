// Preview only. Set exploded_gap=0 for the fully assembled enclosure.
include <parts.scad>
exploded_gap=12;
show_modules=true;
color("#353a40") universal_panel();
for (i=[0:2]) {
    color("#d5a440")
        translate([module_centres[i], 0, carrier_front_z+exploded_gap])
            carrier_shape(i);
    if (show_modules)
        translate([module_centres[i], 0, exploded_gap]) module_reference(i);
}
color([0.2, 0.26, 0.32, 0.45])
    translate([0, 0, panel_t+2*exploded_gap]) rear_base();
