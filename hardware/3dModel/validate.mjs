// Node 20+. Usage: node validate.mjs --wasm /path/to/openscad-wasm/openscad.js
// Exports printable STLs and checks that each part is one connected solid.
import { readFileSync, readdirSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const root = dirname(fileURLToPath(import.meta.url));
const wasmArg = process.argv.indexOf('--wasm');
const packageName = wasmArg < 0 ? 'openscad-wasm'
    : pathToFileURL(resolve(process.argv[wasmArg + 1])).href;
const outputDir = join(root, 'stl');
mkdirSync(outputDir, { recursive: true });
const params = readFileSync(join(root, 'params.scad'), 'utf8');
function dimension(name) {
    const match = params.match(new RegExp(`^${name}\\s*=\\s*([\\d.]+)\\s*;`, 'm'));
    if (!match) throw new Error(`Missing literal enclosure dimension: ${name}`);
    return Number(match[1]);
}
const shellWidth = dimension('case_w'), shellHeight = dimension('case_h');

// Check the curved silhouette, not only the rectangular STL bounds. A post
// can remain inside the bounding box while protruding through a rounded end.
function shellOutlineCheck(points) {
    const excess = Math.max(...points.map(([x,y]) =>
        Math.hypot(Math.max(Math.abs(x)-(shellWidth-shellHeight)/2,0),y)
        -shellHeight/2));
    if (excess > 0.001)
        throw new Error(`Enclosure exceeds capsule outline by ${excess.toFixed(4)} mm`);
    return { outerOutline: 'all mesh vertices inside capsule silhouette',
        maxOutlineExcessMm: Number(excess.toFixed(6)) };
}
const entries = ['panel', 'base', 'carrier_a', 'fit_template',
    'carrier_b', 'fit_template_b', 'power_board_mockup'];
// Keep the 5 mm floor channel between the two 2 mm-high routing rails free.
// This also covers the new shell-post gussets, not only the mounted carriers.
const cableChannelInterference = `
    intersection() {
        rear_base();
        floor_cable_channel();
    }
`;
// Positive-volume intersections must be empty. Intended mating faces touch.
// Reference LCD is only the declared provisional axial envelope.
const interferenceSource = `
include </models/parts.scad>
module fitted_carrier(i) {
    translate([module_centres[i],0,carrier_front_z]) carrier_shape(i);
}
union() {
    ${cableChannelInterference}
    // Also check against the exact tessellated outline used by OpenSCAD.
    difference() {
        rear_base();
        translate([0,0,-eps]) capsule(case_w,case_h,case_depth+2*eps);
    }
    difference() {
        universal_panel();
        translate([0,0,-eps]) capsule(case_w,case_h,case_depth+2*eps);
    }
    intersection() {
        universal_panel();
        translate([0,0,panel_t]) rear_base();
    }
    for(i=[0:2]) {
        intersection() { universal_panel(); fitted_carrier(i); }
        intersection() {
            translate([0,0,panel_t]) rear_base(); fitted_carrier(i);
        }
        intersection() {
            fitted_carrier(i);
            translate([module_centres[i],0,lcd_front_z])
                cylinder(d=display_d+0.1,h=lcd_body_t);
        }
        intersection() {
            universal_panel();
            translate([module_centres[i],0,lcd_front_z])
                cylinder(d=display_d+0.1,h=lcd_body_t);
        }
    }
    for(i=[0:1]) for(j=[i+1:2])
        intersection() { fitted_carrier(i); fitted_carrier(j); }
}
`;

const interferenceSourceB = `
include </models/parts.scad>
module fitted_carrier_b(i) {
    translate([module_centres[i],0,carrier_front_z]) carrier_shape_b(i);
}
union() {
    ${cableChannelInterference}
    intersection() {
        universal_panel_b();
        translate([0,0,panel_t]) rear_base();
    }
    for(i=[0:2]) {
        intersection() { universal_panel_b(); fitted_carrier_b(i); }
        intersection() {
            translate([0,0,panel_t]) rear_base(); fitted_carrier_b(i);
        }
        intersection() {
            fitted_carrier_b(i);
            translate([module_centres[i],0,lcd_front_z])
                cylinder(d=b_display_d+0.1,h=lcd_body_t);
        }
        intersection() {
            universal_panel_b();
            translate([module_centres[i],0,lcd_front_z])
                cylinder(d=b_display_d+0.1,h=lcd_body_t);
        }
    }
    for(i=[0:1]) for(j=[i+1:2])
        intersection() { fitted_carrier_b(i); fitted_carrier_b(j); }
}
`;

const electronicsInterferenceSource = `
include </models/parts.scad>
module gauge_rear_envelope(i) {
    translate([module_centres[i],0,pcb_front_z-panel_t])
        linear_extrude(height=pcb_t+pcb_rear_component_h)
            scale([board_envelope_w/2,board_envelope_h/2]) circle(d=2);
}
union() {
    ${cableChannelInterference}
    intersection() { rear_base(); aux_board_envelope(); }
    intersection() { rear_base(); power_connector_envelope(); }
    for(j=[0:2]) {
        intersection() { gauge_rear_envelope(j); aux_board_envelope(); }
        intersection() { gauge_rear_envelope(j); power_connector_envelope(); }
    }
}
`;
const checkJobs = ['interference', 'interference_b', 'electronics_interference'];

const powerUsbInterferenceSource = `
include </models/parts.scad>
union() {
    intersection() { rear_base(); power_usb_shell(); }
    intersection() { rear_base(); power_usb_external_sweep(); }
    // Four independent witness volumes require the full original wall
    // thickness above, below and beside the aperture (no thin mask).
    for (z=[usb_z-usb_h/2-0.9,usb_z+usb_h/2+0.3])
        difference() {
            translate([-4,usb_wall_face_y+0.02,z]) cube([8,wall_t-0.04,0.6]);
            rear_base();
        }
    for (x=[-usb_w/2-0.9,usb_w/2+0.3])
        difference() {
            translate([x,usb_wall_face_y+0.02,usb_z-1]) cube([0.6,wall_t-0.04,2]);
            rear_base();
        }
    // Check through-air paths independently of the slot-cutting module.
    for(x=vent_centres_x) for(y=vent_centres_y)
        intersection() {
            rear_base();
            translate([x,y,case_depth-bottom_t-eps])
                cylinder(d=vent_slot_w/2,h=bottom_t+2*eps);
        }
}
`;

// Individual A and B fit is checked above. Check every cross-profile pair
// at all positions: these pairs cover all six mixed A/B configurations.
const mixedInterferenceSource = `
include </models/parts.scad>
module fitted(i,p) {
    translate([module_centres[i],0,carrier_front_z])
        carrier_shape_profile(profile=p);
}
union() {
    for(i=[0:1]) for(j=[i+1:2]) for(p=[profile_a,profile_b])
        intersection() { fitted(i,p); fitted(j,1-p); }
}
`;

const usbInterferenceSource = `
include </models/parts.scad>
module fitted(i,p) {
    translate([module_centres[i],0,carrier_front_z])
        carrier_shape_profile(profile=p);
}
module installed(i) {
    translate([module_centres[i],0,0]) {
        usb_plug_envelope(); usb_cable_envelope(index=i);
    }
}
module unplug_sweep(i) {
    translate([module_centres[i],0,0])
        usb_plug_envelope(travel=usb_unmate_travel);
}
union() {
    // Regression: both full carriers and calibration templates must leave
    // the entire 14 mm-wide six-o'clock slot clear through their thickness.
    for(p=[profile_a,profile_b]) for(template=[false,true])
        intersection() {
            carrier_shape_profile(profile=p,template=template,
                                  thickness=template ? 1 : carrier_h);
            translate([0,0,-eps]) linear_extrude(height=carrier_h+2*eps)
                carrier_usb_opening_2d();
        }
    // Preserve the front face; only the rear locating collar is opened.
    for(cx=module_centres)
        intersection() {
            universal_panel();
            translate([cx,0,panel_t+eps])
                linear_extrude(height=panel_collar_h)
                    carrier_usb_opening_2d();
        }
    for(i=[0:2]) {
        intersection() { installed(i); universal_panel(); }
        intersection() { installed(i); translate([0,0,panel_t]) rear_base(); }
        intersection() { installed(i); translate([0,0,panel_t]) aux_board_envelope(); }
        intersection() { installed(i); translate([0,0,panel_t]) power_connector_envelope(); }
        // Opening the rear box removes its wall and power boards from the
        // insertion path. They are intentionally NOT omitted from installed().
        intersection() { unplug_sweep(i); universal_panel(); }
        for(j=[0:2]) for(p=[profile_a,profile_b]) {
            intersection() { unplug_sweep(i); fitted(j,p); }
            intersection() { installed(i); fitted(j,p); }
        }
        // Receiver housing and its mating face are checked against printed
        // parts too. The male mating portion is intentionally inside it.
        intersection() {
            translate([module_centres[i],0,0]) board_usb_reference();
            universal_panel();
        }
        for(p=[profile_a,profile_b])
            intersection() {
                translate([module_centres[i],0,0]) board_usb_reference();
                fitted(i,p);
            }
    }
}
`;
checkJobs.push('mixed_interference', 'usb_interference', 'power_usb_interference');

async function render(name) {
    const messages = [];
    try {
        const { createOpenSCAD } = await import(packageName);
        const cad = await createOpenSCAD({
            noInitialRun: true,
            print: t => messages.push(t),
            printErr: t => messages.push(t),
        });
        const engine = cad.getInstance();
        engine.FS.mkdir('/models');
        for (const source of readdirSync(root).filter(n => n.endsWith('.scad')))
            engine.FS.writeFile(`/models/${source}`, readFileSync(join(root, source), 'utf8'));
        const interference = checkJobs.includes(name);
        const source = name === 'power_usb_interference' ? powerUsbInterferenceSource
            : name === 'usb_interference' ? usbInterferenceSource
            : name === 'mixed_interference' ? mixedInterferenceSource
            : name === 'electronics_interference' ? electronicsInterferenceSource
            : name === 'interference_b' ? interferenceSourceB : interferenceSource;
        engine.FS.writeFile('/job.scad', interference ? source
            : `include </models/${name}.scad>\n`);
        const status = engine.callMain(['/job.scad', '-o', `/${name}.stl`]);
        if (interference) {
            // CGAL counts the unbounded exterior as one volume. Volumes=1
            // means contact faces/edges only, as with two cubes touching.
            const contactsOnly = messages.some(t => /Volumes:\s+1\s*$/.test(t));
            const contactWarning = /^(?:EXPORT-WARNING: Exported object|WARNING: Object) may not be a valid 2-manifold and may need repair!?$/;
            const errors = messages.filter(t => /ERROR:|WARNING:/.test(t)
                && !(contactsOnly && contactWarning.test(t)));
            if (errors.length) throw new Error('OpenSCAD interference diagnostic');
            if (!contactsOnly && !messages.some(t => /Current top level object is empty/.test(t))) {
                const diagnostic = engine.FS.readFile(`/${name}.stl`, { encoding:'utf8' });
                const points = [...diagnostic.matchAll(/vertex\s+([-+\d.eE]+)\s+([-+\d.eE]+)\s+([-+\d.eE]+)/g)]
                    .map(m=>m.slice(1).map(Number));
                const bounds = [0,1,2].map(axis=>points.reduce(
                    ([lo,hi],p)=>[Math.min(lo,p[axis]),Math.max(hi,p[axis])],[Infinity,-Infinity]));
                throw new Error(`Assembled solids interfere; intersection bounds XYZ=${JSON.stringify(bounds)}`);
            }
            return { name, assembledInterference: 'no positive-volume overlap',
                zeroThicknessContacts: contactsOnly,
                ...(name === 'power_usb_interference' ? {
                    input: 'board-mounted USB-C; no extension module',
                    socketExposure: '0.2 mm nominal beyond continuous exterior wall',
                    aperture: '9.74x3.96 mm, R1.8; full 2.4 mm wall; no recess or thin mask',
                    externalInsertion: '12x8 mm complete plug housing; 25 mm straight travel clear',
                    wallWitnesses: 'full wall thickness present above, below and on both sides',
                    ventilation: '12 slots; independent through-floor air paths clear',
                } : name === 'usb_interference' ? {
                    usbService: 'unplug only with rear box removed',
                    installedPlugAndCable: 'clear including 0.8 mm margin',
                    unplugSweep: '6.5 mm travel clear with rear box removed',
                    envelope: '12 mm wide, 10 mm below mating face, 14 mm behind PCB rear',
                    bUsbPosition: 'A STEP envelope reused; confirm B hardware',
                    carrierAndTemplateSlots: 'A/B: full 14 mm slot clear through thickness',
                    panelCollarSlots: 'rear slots clear; front face retained',
                } : name === 'mixed_interference' ? {
                    mixedProfiles: 'all six mixed A/B combinations covered by cross-profile pairs',
                } : {
                    floorCableChannel: '5 mm channel has no positive-volume obstruction',
                    ...(name === 'electronics_interference' ? {
                        powerBoard: 'R3 82x55.8x1.2 mm, central tongue 12x0.3 mm, origin=hole midpoint, holes +/-37 mm',
                        components: '7.2 mm front; 2 mm rear including solder; 2.5 mm standoffs',
                        connectors: 'XH mated leads Y<=37 mm; direct external USB-C, 12x8 mm housing',
                        wireless: 'removed; magnetic mounting recess retained',
                    } : {}),
                }),
                physicalFit: 'not verified' };
        }
        if (messages.some(t => /ERROR:|WARNING:/.test(t)))
            throw new Error('OpenSCAD diagnostic');
        if (status || !messages.some(t => /Simple:\s+yes/.test(t))
            || !messages.some(t => /Volumes:\s+2\s*$/.test(t)))
            throw new Error('Expected one closed solid');
        const stl = engine.FS.readFile(`/${name}.stl`, { encoding: 'utf8' });
        const points = [...stl.matchAll(/vertex\s+([-+\d.eE]+)\s+([-+\d.eE]+)\s+([-+\d.eE]+)/g)]
            .map(m => m.slice(1).map(Number));
        if (!points.length) throw new Error('No STL triangles');
        const bounds = [0, 1, 2].map(axis => points.reduce(
            ([lo, hi], p) => [Math.min(lo, p[axis]), Math.max(hi, p[axis])],
            [Infinity, -Infinity]));
        if (Math.abs(bounds[2][0]) > 0.001)
            throw new Error('Print orientation does not sit on Z=0');
        const outline = ['panel','base'].includes(name) ? shellOutlineCheck(points) : {};
        writeFileSync(join(outputDir, `${name}.stl`), stl);
        return { name, connectedSolids: 1, bounds, triangles: points.length / 3, ...outline };
    } catch (error) {
        throw new Error(`${name}: ${String(error)}\n${messages.join('\n')}`);
    }
}

const jobArg = process.argv.indexOf('--job');
if (jobArg >= 0) {
    const name = process.argv[jobArg + 1];
    if (![...entries, ...checkJobs].includes(name))
        throw new Error('Unknown CAD job');
    console.log(JSON.stringify(await render(name)));
    process.exit(0);
}

// Emscripten/CGAL state is not reusable after callMain. Isolate each export.
// --jobs 2 can run independent checks concurrently, each in its own process.
const jobsArg = process.argv.indexOf('--jobs');
const parallelism = jobsArg < 0 ? 1 : Number(process.argv[jobsArg+1]);
if (!Number.isInteger(parallelism) || parallelism < 1 || parallelism > 4)
    throw new Error('--jobs must be an integer between 1 and 4');
const runChild = promisify(execFile);
const jobs = [...entries,...checkJobs];
const report = new Array(jobs.length);
let nextJob = 0;
async function worker() {
    while (nextJob < jobs.length) {
        const index = nextJob++, name = jobs[index];
        console.log(checkJobs.includes(name) ? `Checking ${name} ...`
            : `Rendering ${name} ...`);
        const { stdout } = await runChild(process.execPath,
            [fileURLToPath(import.meta.url), ...process.argv.slice(2), '--job', name],
            { encoding:'utf8', maxBuffer:8*1024*1024 });
        const result = JSON.parse(stdout);
        report[index] = result;
        if (result.bounds)
            console.log(`${name}: one closed solid, Z=${result.bounds[2].join('..')} mm`);
        else console.log(`${name}: PASS`);
    }
}
const workers = await Promise.allSettled(
    Array.from({length:Math.min(parallelism,jobs.length)},()=>worker()));
const failures = workers.filter(r=>r.status==='rejected');
if (failures.length) throw new AggregateError(failures.map(r=>r.reason),'CAD validation failed');
writeFileSync(join(outputDir, 'validation.json'), `${JSON.stringify(report, null, 2)}\n`);
console.log('PASS: A/B and mixed solids, USB service path, electronics and cable route do not overlap.');
console.log('PCB holes and LCD-to-PCB stack still require measurement on the real module.');
