// Print first. Rear-view XY hole check; coordinates follow the supplied STEP.
// Keep this face UP for checking: rotating a printed carrier flips its Y axis.
include <parts.scad>
carrier_shape(thickness=1.0, template=true);
