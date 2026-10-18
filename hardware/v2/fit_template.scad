// Print first. Rear-view XY hole check; holes are still photo estimates.
// Keep this face UP for checking: rotating a printed carrier flips its Y axis.
include <parts.scad>
module_index=1;
carrier_shape(module_index, thickness=1.0, template=true);
