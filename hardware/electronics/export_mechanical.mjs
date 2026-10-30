// Generate mechanical DXF outlines and interface metadata from params.scad.
// These files contain no routed copper or production electrical schematic.
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const out = dirname(fileURLToPath(import.meta.url));
const source = readFileSync(join(out, '..', '3dModel', 'params.scad'), 'utf8');
function number(name) {
    const match = source.match(new RegExp(`^${name}\\s*=\\s*([-+\\d.]+)\\s*;`, 'm'));
    if (!match || !Number.isFinite(Number(match[1])))
        throw new Error(`Expected numeric mechanical parameter: ${name}`);
    return Number(match[1]);
}
const centre = JSON.parse(source.match(/^power_pcb_centre\s*=\s*(\[[^;]+);/m)[1]);
const pcb = JSON.parse(readFileSync(join(out,'power_pd_3x5v_r3','mechanical_interface.json'),'utf8'));
const outline = JSON.parse(source.match(/^power_pcb_outline_xy\s*=\s*(\[[^;]+);/m)[1]);
if (pcb.board.width !== number('power_pcb_w') || pcb.board.height !== number('power_pcb_h')
    || pcb.board.thickness !== number('power_pcb_t')
    || pcb.mounting_holes[1].xy[0]-pcb.mounting_holes[0].xy[0] !== number('power_hole_pitch'))
    throw new Error('Enclosure and current PCB mechanical contracts differ');
if (JSON.stringify(outline) !== JSON.stringify(pcb.board.outline_xy))
    throw new Error('Enclosure and PCB outline vertices differ');
if (number('aux_post_h') < pcb.component_height_targets.back_component_clearance_required_mm
    || number('aux_post_h') < number('power_backside_h')+0.5)
    throw new Error('PCB backside components lack floor clearance');
const usbFace = centre[1]+pcb.connectors.find(c=>c.ref==='J1').shell_mating_face_y_mm;
const wallFace = -number('case_h')/2;
const usbPlaneZ = number('case_depth')-number('bottom_t')-number('aux_post_h')
    -number('power_pcb_t')-number('power_usb_axis_above_pcb');
if (Math.abs(usbFace-number('power_usb_face_y'))>0.001
    || Math.abs(wallFace-usbFace-number('usb_face_exposure'))>0.001)
    throw new Error('USB-C face does not protrude through the continuous wall');
if (number('usb_w')<number('power_usb_shell_w')+0.8-0.001
    || number('usb_h')<number('power_usb_mouth_h')+0.8-0.001)
    throw new Error('USB-C mouth lacks 0.4 mm per-side printing clearance');
if (number('usb_w') < number('power_usb_tip_w')
    || number('usb_h') < number('power_usb_tip_h'))
    throw new Error('USB-C metal tip cannot pass the socket aperture');
function makeDxf(outline, holes) {
// R2000 readers need symbol-table handles, the declared linetype and model/
// paper-space block records, even for a drawing containing only eight entities.
const dxf = ['0','SECTION','2','HEADER','9','$ACADVER','1','AC1015',
    '9','$INSUNITS','70','4','9','$HANDSEED','5','FF',
    '0','ENDSEC','0','SECTION','2','TABLES',
    '0','TABLE','2','LTYPE','5','1','330','0','100','AcDbSymbolTable','70','1',
    '0','LTYPE','5','2','330','1','100','AcDbSymbolTableRecord',
    '100','AcDbLinetypeTableRecord','2','CONTINUOUS','70','0',
    '3','Solid line','72','65','73','0','40','0','0','ENDTAB',
    '0','TABLE','2','LAYER','5','3','330','0','100','AcDbSymbolTable','70','3'];
for (const [handle, name, colour] of [
    ['4','0','7'], ['5','BOARD_OUTLINE','7'], ['6','MOUNTING_HOLES','1']])
    dxf.push('0','LAYER','5',handle,'330','3','100','AcDbSymbolTableRecord',
        '100','AcDbLayerTableRecord','2',name,'70','0','62',colour,'6','CONTINUOUS');
dxf.push('0','ENDTAB','0','TABLE','2','BLOCK_RECORD','5','7','330','0',
    '100','AcDbSymbolTable','70','2');
for (const [handle, name] of [['8','*Model_Space'], ['9','*Paper_Space']])
    dxf.push('0','BLOCK_RECORD','5',handle,'330','7','100','AcDbSymbolTableRecord',
        '100','AcDbBlockTableRecord','2',name);
dxf.push('0','ENDTAB','0','ENDSEC','0','SECTION','2','BLOCKS');
for (const [begin, end, owner, name] of [
    ['A','B','8','*Model_Space'], ['C','D','9','*Paper_Space']]) {
    dxf.push('0','BLOCK','5',begin,'330',owner,'100','AcDbEntity','8','0',
        '100','AcDbBlockBegin','2',name,'70','0','10','0','20','0','30','0',
        '3',name,'1','',
        '0','ENDBLK','5',end,'330',owner,'100','AcDbEntity','8','0','100','AcDbBlockEnd');
}
dxf.push('0','ENDSEC','0','SECTION','2','ENTITIES');
let nextHandle = 0x20;
function entity(type, layer, subclass) {
    return ['0',type,'5',(nextHandle++).toString(16).toUpperCase(),
        '330','8','100','AcDbEntity','8',layer,'100',subclass];
}
for (let i=0; i<outline.length; i++) {
    const a=outline[i], b=outline[(i+1)%outline.length];
    dxf.push(...entity('LINE','BOARD_OUTLINE','AcDbLine'),'10',String(a[0]),'20',String(a[1]),
        '30','0','11',String(b[0]),'21',String(b[1]),'31','0');
}
for (const p of holes)
    dxf.push(...entity('CIRCLE','MOUNTING_HOLES','AcDbCircle'),'10',String(p[0]),'20',String(p[1]),
        '30','0','40',String(number('aux_hole_d')/2));
dxf.push('0','ENDSEC','0','EOF');
return `${dxf.join('\n')}\n`;
}

const common = {
    units: 'mm', stage: 'enclosure mechanical interface; verify physical assembly',
    material: 'rigid FR-4',
    localOrigin: 'mounting-hole midpoint; X=KiCad x-100, Y=100-KiCad y',
    componentSide: 'faces the gauge modules; confirm board flip in PCB CAD',
    standoffHeight:number('aux_post_h'),
    standoffDiameter:number('aux_post_d'),
    standoffRootDiameter:number('aux_post_root_d'),
    insert: {thread:'M2', length:number('m2_insert_length'), outsideDiameter:number('m2_insert_outer_d'),
        pilotDiameter:number('m2_insert_d'), blindDepth:number('m2_insert_depth')},
    screw: 'M2×4 pan/cylinder head; verify head height and engagement on the sample',
};
const boards = [
    {...common, name:'power_board', centre,
        width:number('power_pcb_w'), height:number('power_pcb_h'), thickness:number('power_pcb_t'),
        outlineXY:outline,
        mountingXY:[[-number('power_hole_pitch')/2,0],[number('power_hole_pitch')/2,0]],
        componentHeightMax:number('power_components_h'), undersideSolderHeightMax:number('power_backside_h'),
        electricalDesign:'power_pd_3x5v_r3/power_pd_3x5v_r3.xh.kicad_pcb',
        currentPcbMechanicalInterface:'power_pd_3x5v_r3/mechanical_interface.json',
        pcbRevision:pcb.revision, pcbSourceSha256:pcb.pcb_sha256,
        mountingInterfaceMatches:true,
        physicalFit:'numerical envelope validation supplied separately; physical sample fit pending',
        wirelessReservation:false,
        magneticMountingRecessRetained:true,
        ventilation:{slotWidth:number('vent_slot_w'),slotHeight:number('vent_slot_h'),
            cornerRadius:number('vent_slot_r'),throughFloor:true,
            centresXY:JSON.parse(source.match(/^vent_centres_x\s*=\s*(\[[^;]+);/m)[1])
                .flatMap(x=>JSON.parse(source.match(/^vent_centres_y\s*=\s*(\[[^;]+);/m)[1])
                    .map(y=>[x,y]))},
        interfaces:[
            {name:'J1_USB_PD_IN', matingFaceY:number('power_usb_face_y'),
                shellHole:{width:number('usb_w'),height:number('usb_h')},
                visibleAperture:{width:number('usb_w'),height:number('usb_h')},
                exteriorWallFaceY:wallFace, socketExposure:number('usb_face_exposure'),
                wall:{thickness:number('wall_t'),continuous:true,recess:false,thinMask:false},
                mouth:{width:number('power_usb_shell_w'),height:number('power_usb_mouth_h'),
                    axisAbovePcb:number('power_usb_axis_above_pcb'),printingClearancePerSide:0.4},
                axisZFromOuterFloor:Number((number('case_depth')-usbPlaneZ).toFixed(3)),
                axisZFromInnerFloor:Number((number('case_depth')-number('bottom_t')-usbPlaneZ).toFixed(3)),
                plugMax:{width:number('power_usb_plug_w'),extension:number('power_usb_plug_down'),height:number('power_usb_plug_h')},
                role:'complete 12x8 mm plug housing stays outside continuous wall; retain CC/DP/DM'},
            {name:'J2_J3_J4_XH', housingFaceY:number('power_xh_front_y'),
                matedWireLimitY:number('power_xh_wire_limit_y'),
                pinout:{1:'+5V',2:'GND'}, role:'wires must turn toward front before reaching upper wall'},
        ], connectorStatus:'final PCB Fab bounds checked; purchased mating plugs and cable bends still require trial fit'},
];
for (const board of boards) {
    board.holes=board.mountingXY.map(xy=>({xy, diameter:number('aux_hole_d'), type:'NPTH',
        copperAndComponentKeepoutDiameter:number('aux_hole_keepout_d')}));
    board.enclosureHoleCentres=board.mountingXY.map(([x,y])=>[x+board.centre[0],y+board.centre[1]]);
    const bodyDepth=number('case_depth'), floor=number('bottom_t'), post=number('aux_post_h');
    const moduleRear=number('panel_t')+number('panel_post_h')+number('carrier_h')
        +number('pcb_t')+number('pcb_rear_component_h');
    board.componentPlaneZ=number('panel_t')+bodyDepth-floor-post-board.thickness;
    board.gaugeClearance=Number((board.componentPlaneZ-board.componentHeightMax-moduleRear).toFixed(3));
    if(board.gaugeClearance<2) throw new Error(`${board.name}: insufficient height budget`);
    board.shellThickness=number('panel_t')+bodyDepth;
    const gaugePlugRear=number('panel_t')+number('panel_post_h')+number('carrier_h')
        +number('pcb_t')+number('usb_plug_rear');
    const cableLimit=gaugePlugRear+number('usb_cable_d')/2+number('usb_plug_clearance')
        +board.componentHeightMax+board.thickness+post+floor;
    board.thicknessLimits={
        bareModuleStackOnly:Number((moduleRear+2+board.componentHeightMax+board.thickness+post+floor).toFixed(3)),
        centralGaugeCable:Number(cableLimit.toFixed(3)),
        inputCollar:'removed; continuous original side wall only',
        scope:'declared plug/cable envelopes; battery, speaker and physical bend radius still require trial fit',
    };
    board.minimumShellThickness=Math.ceil(cableLimit);
    if(board.shellThickness<board.minimumShellThickness)
        throw new Error('Shell too thin for central gauge USB cable');
    writeFileSync(join(out, `${board.name}_outline.dxf`),
        makeDxf(board.outlineXY,board.mountingXY));
    writeFileSync(join(out, `${board.name}_interface.json`), `${JSON.stringify(board,null,2)}\n`);
}
console.log(`Exported power ${boards[0].width}×${boards[0].height} mm / ${boards[0].holes.length} holes; `+
    `central XY=${centre}; wireless reservation removed; `+
    `shell ${boards[0].shellThickness} mm, power clearance ${boards[0].gaugeClearance} mm.`);
