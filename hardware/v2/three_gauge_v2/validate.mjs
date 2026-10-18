// Node 20+. Usage: node validate.mjs --wasm /path/to/openscad-wasm/openscad.js
// Exports printable STLs and checks that each part is one connected solid.
import { readFileSync, readdirSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = dirname(fileURLToPath(import.meta.url));
const wasmArg = process.argv.indexOf('--wasm');
const packageName = wasmArg < 0 ? 'openscad-wasm'
    : pathToFileURL(resolve(process.argv[wasmArg + 1])).href;
const outputDir = join(root, 'stl');
mkdirSync(outputDir, { recursive: true });
const entries = ['panel', 'base', 'carrier_left', 'carrier_centre',
    'carrier_right', 'fit_template'];
// Positive-volume intersections must be empty. Intended mating faces touch.
// Reference LCD is only the declared provisional axial envelope.
const interferenceSource = `
include </models/parts.scad>
module fitted_carrier(i) {
    translate([module_centres[i],0,carrier_front_z]) carrier_shape(i);
}
union() {
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
        engine.FS.writeFile('/job.scad', name === 'interference' ? interferenceSource
            : `include </models/${name}.scad>\n`);
        const status = engine.callMain(['/job.scad', '-o', `/${name}.stl`]);
        if (name === 'interference') {
            // CGAL counts the unbounded exterior as one volume. Volumes=1
            // means contact faces/edges only, as with two cubes touching.
            const contactsOnly = messages.some(t => /Volumes:\s+1\s*$/.test(t));
            const contactWarning = /^(?:EXPORT-WARNING: Exported object|WARNING: Object) may not be a valid 2-manifold and may need repair!?$/;
            const errors = messages.filter(t => /ERROR:|WARNING:/.test(t)
                && !(contactsOnly && contactWarning.test(t)));
            if (errors.length) throw new Error('OpenSCAD interference diagnostic');
            if (!contactsOnly && !messages.some(t => /Current top level object is empty/.test(t)))
                throw new Error('Assembled solids interfere');
            return { assembledInterference: 'no positive-volume overlap',
                zeroThicknessContacts: contactsOnly, physicalFit: 'not verified' };
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
        writeFileSync(join(outputDir, `${name}.stl`), stl);
        return { name, connectedSolids: 1, bounds, triangles: points.length / 3 };
    } catch (error) {
        throw new Error(`${name}: ${String(error)}\n${messages.join('\n')}`);
    }
}

const jobArg = process.argv.indexOf('--job');
if (jobArg >= 0) {
    const name = process.argv[jobArg + 1];
    if (![...entries, 'interference'].includes(name)) throw new Error('Unknown CAD job');
    console.log(JSON.stringify(await render(name)));
    process.exit(0);
}

// Emscripten/CGAL state is not reusable after callMain. Isolate each export.
const report = [];
for (const name of [...entries, 'interference']) {
    console.log(name === 'interference' ? 'Checking assembled part clearances ...'
        : `Rendering ${name} ...`);
    const result = JSON.parse(execFileSync(process.execPath,
        [fileURLToPath(import.meta.url), ...process.argv.slice(2), '--job', name],
        { encoding: 'utf8', maxBuffer: 8 * 1024 * 1024 }));
    report.push(result);
    if (result.bounds)
        console.log(`${name}: one closed solid, Z=${result.bounds[2].join('..')} mm`);
}
writeFileSync(join(outputDir, 'validation.json'), `${JSON.stringify(report, null, 2)}\n`);
console.log('PASS: printable single solids; declared assembly envelopes do not overlap.');
console.log('PCB holes and LCD-to-PCB stack still require measurement on the real module.');
