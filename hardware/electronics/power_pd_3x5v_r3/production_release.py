#!/usr/bin/env python3
"""Package/verify the frozen R3 production data; never regenerate or edit KiCad sources."""
import argparse
import csv
import hashlib
import json
import math
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PRODUCTION = ROOT / 'production'
VERIFY = ROOT / 'verification'
ARCHIVE = ROOT / 'release/production-20261026-r1.zip'
VERSION = 'R3-PRODUCTION-20261026-R1'
ASSEMBLY = {
    'BOM_HUAQIU_ALL.csv': 'BOM_ALL.csv',
    'BOM_HUAQIU_SMT.csv': 'BOM_SMT.csv',
    'BOM_HUAQIU_THT.csv': 'BOM_THT.csv',
    'CPL_SMT.csv': 'CPL_SMT.csv',
    'THT_POSITION.csv': 'THT_POSITION.csv',
    'PIN1_POLARITY.csv': 'PIN1_POLARITY.csv',
    'ASSEMBLY_TOP_BOTTOM.pdf': 'ASSEMBLY_TOP_BOTTOM.pdf',
}
PAYLOAD = {'PCB_GERBER.zip', 'README.md', 'ORDER_PARAMETERS.md',
           'ASSEMBLY_NOTES.md', 'FIRST_ARTICLE_TEST.md', 'pcb/VIPPO_HOLES.csv'} | {
    'assembly/' + name for name in ASSEMBLY.values()
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def csv_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def import_legacy(legacy):
    """One-time migration of verified manufacturing exports, without source parsing."""
    manifest = read_json(legacy / 'SHA256.json')
    names = ['PCB_GERBER.zip'] + ['assembly/' + name for name in ASSEMBLY]
    verification = ['BOARD_GEOMETRY.json', 'ASSEMBLY_VALIDATION.json',
                    'ERC_VERIFICATION.json', 'NETLIST_COMPARISON.json',
                    'VIA_IN_PAD_REVIEW.json', 'IC_ELECTRICAL_REVIEW.md']
    names += ['verification/' + name for name in verification]
    names += ['assembly/BOM_BY_REFERENCE.csv', 'schematic/SCHEMATIC.pdf']
    pdfs = sorted((legacy / 'verification/manufacturer-evidence').glob('*.pdf'))
    names += [str(path.relative_to(legacy)) for path in pdfs]
    for name in names:
        require(digest((legacy / name).read_bytes()) == manifest['files'][name],
                'Legacy checksum mismatch: ' + name)
    (PRODUCTION / 'assembly').mkdir(parents=True, exist_ok=True)
    VERIFY.mkdir(parents=True, exist_ok=True)
    shutil.copy2(legacy / 'PCB_GERBER.zip', PRODUCTION / 'PCB_GERBER.zip')
    for old, new in ASSEMBLY.items():
        shutil.copy2(legacy / 'assembly' / old, PRODUCTION / 'assembly' / new)
    for name in verification:
        shutil.copy2(legacy / 'verification' / name, VERIFY / name)
    shutil.copy2(legacy / 'assembly/BOM_BY_REFERENCE.csv', VERIFY / 'BOM_BY_REFERENCE.csv')
    shutil.copy2(legacy / 'schematic/SCHEMATIC.pdf', VERIFY / 'SCHEMATIC.pdf')
    (VERIFY / 'datasheets').mkdir(exist_ok=True)
    for path in pdfs:
        shutil.copy2(path, VERIFY / 'datasheets' / path.name)
    vip = read_json(VERIFY / 'VIA_IN_PAD_REVIEW.json')
    grouped = {}
    for item in vip['intersections']:
        grouped.setdefault(item['via_id'], []).append(item)
    (PRODUCTION / 'pcb').mkdir(exist_ok=True)
    with (PRODUCTION / 'pcb/VIPPO_HOLES.csv').open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.writer(stream)
        writer.writerow(['Via_ID', 'Gerber_X_mm', 'Gerber_Y_mm', 'CPL_X_mm',
                         'CPL_Y_mm', 'Drill_mm', 'Net', 'Pads_to_review'])
        for key, items in sorted(grouped.items()):
            item = items[0]
            x, y = item['via_xy']
            writer.writerow([key, f'{x:.6f}', f'{-y:.6f}', f'{x-59:.6f}',
                             f'{127.5-y:.6f}', item['drill_mm'], item['net'],
                             '; '.join(sorted({r['ref'] + '.' + (r['pad'] or 'paste') for r in items}))])
    old_zip = legacy.with_suffix('.zip')
    write_json(VERIFY / 'IMPORT_PROVENANCE.json', {
        'imported_from': 'erc-20261026.zip', 'legacy_zip_sha256': digest(old_zip.read_bytes()),
        'pcb_sha256': manifest['pcb_sha256'],
        'unchanged_exports': {
            'PCB_GERBER.zip': manifest['files']['PCB_GERBER.zip'],
            **{'assembly/' + new: manifest['files']['assembly/' + old] for old, new in ASSEMBLY.items()},
        },
    })


def verify_data():
    geometry = read_json(VERIFY / 'BOARD_GEOMETRY.json')
    erc = read_json(VERIFY / 'ERC_VERIFICATION.json')
    drc = read_json(VERIFY / 'DRC_VERIFIED.json')
    comparison = read_json(VERIFY / 'NETLIST_COMPARISON.json')
    current_erc = read_json(VERIFY / 'current/power_pd_3x5v_r3.xh-erc.json')
    current_drc = read_json(VERIFY / 'current/power_pd_3x5v_r3.xh-drc.json')
    require(not current_drc['violations'] and not current_drc['unconnected_items'], 'Current DRC failed')
    require(all(not sheet['violations'] for sheet in current_erc['sheets']), 'Current ERC failed')
    require(drc['violation_count'] == drc['unconnected_count'] == 0, 'Bound DRC failed')
    require(geometry['sha256'] == drc['source_sha256'] == comparison['pcb_sha256'], 'Board hashes differ')
    require(erc['pass'] and comparison['pass'] and comparison['electrical_connectivity_match'], 'Connection audit failed')
    # Binary fingerprints only; all KiCad inspection/export remains through MCP.
    for name, expected in erc['source_hashes'].items():
        require(digest((ROOT / name).read_bytes()) == expected, 'Source changed; re-export through KiCad MCP: ' + name)
    require(digest((ROOT / 'power_pd_3x5v_r3.xh.kicad_pro').read_bytes()) == drc['project_rules_sha256'], 'Rules changed')
    provenance = read_json(VERIFY / 'IMPORT_PROVENANCE.json')
    for name, expected in provenance['unchanged_exports'].items():
        require(digest((PRODUCTION / name).read_bytes()) == expected, 'Frozen export changed: ' + name)
    with zipfile.ZipFile(PRODUCTION / 'PCB_GERBER.zip') as archive:
        require(archive.testzip() is None and len(archive.namelist()) == 14, 'Gerber ZIP damaged/incomplete')
        job = json.loads(archive.read(next(name for name in archive.namelist() if name.endswith('.gbrjob'))))
        require(sum(f['FileFunction'].startswith('Copper,') for f in job['FilesAttributes']) == 4, 'Expected 4 copper layers')
        require(all(item['Path'] in archive.namelist() for item in job['FilesAttributes']), 'Missing Gerber layer')
        for suffix in ['-F_Paste.gtp', '-B_Paste.gbp', '-PTH.drl', '-NPTH.drl', '-Edge_Cuts.gm1']:
            require(sum(name.endswith(suffix) for name in archive.namelist()) == 1, 'Missing/duplicate ' + suffix)
    parts = csv_rows(VERIFY / 'BOM_BY_REFERENCE.csv')
    require(len(parts) == 86 and len({r['Ref'] for r in parts}) == 86, 'Expected 86 unique parts')
    require(len({(r['Manufacturer'], r['MPN']) for r in parts}) == 31, 'Expected 31 procurement identities')
    catalog = {}
    for group in read_json(ROOT / 'assembly_catalog.json')['groups']:
        for ref in group['refs']:
            require(ref not in catalog, 'Duplicate procurement reference: ' + ref)
            catalog[ref] = (group['manufacturer'], group['mpn'], group['value'], group['mount'])
    require(catalog == {r['Ref']: (r['Manufacturer'], r['MPN'], r['Value'], r['Mount']) for r in parts},
            'Procurement catalog differs from approved production BOM')
    for filename, mount in [('BOM_ALL.csv', None), ('BOM_SMT.csv', 'SMT'), ('BOM_THT.csv', 'THT')]:
        found = {}
        for row in csv_rows(PRODUCTION / 'assembly' / filename):
            refs = [ref.strip() for ref in row['位号*'].split(',')]
            require(len(refs) == int(row['单机用量*']), 'BOM quantity mismatch')
            for ref in refs:
                require(ref not in found, 'Duplicate BOM reference: ' + ref)
                found[ref] = (row['品牌'], row['型号*'])
        expected = {r['Ref']: (r['Manufacturer'], r['MPN']) for r in parts if mount is None or r['Mount'] == mount}
        require(found == expected, 'BOM coverage/MPN mismatch: ' + filename)
    footprints = {item['ref']: item for item in geometry['footprints']}
    for filename, mount, count in [('CPL_SMT.csv', 'SMT', 82), ('THT_POSITION.csv', 'THT', 4)]:
        positions = csv_rows(PRODUCTION / 'assembly' / filename)
        expected = {item['Ref'] for item in parts if item['Mount'] == mount}
        require(len(positions) == count and {r['Designator'] for r in positions} == expected, 'CPL coverage mismatch')
        if mount == 'SMT':
            require(sum(r['Layer'] == 'Top' for r in positions) == 31, 'Top count mismatch')
            require(sum(r['Layer'] == 'Bottom' for r in positions) == 51, 'Bottom count mismatch')
        for row in positions:
            footprint = footprints[row['Designator']]
            require(math.isclose(float(row['Mid X']), footprint['at'][0]-59, abs_tol=1e-5) and
                    math.isclose(float(row['Mid Y']), 127.5-footprint['at'][1], abs_tol=1e-5), 'CPL coordinate mismatch')
            require(math.isclose((float(row['Rotation'])-footprint['angle']+180) % 360-180, 0, abs_tol=1e-5), 'CPL angle mismatch')
            require(row['Layer'] == ('Top' if footprint['layer'] == 'F.Cu' else 'Bottom'), 'CPL side mismatch')
    require(next(p for p in parts if p['Ref'] == 'J1')['MPN'] == 'USB4085-GF-A', 'Incorrect J1')
    for row in csv_rows(PRODUCTION / 'assembly/PIN1_POLARITY.csv'):
        pad = next(p for p in footprints[row['Ref']]['pads'] if p['number'] == row['Pad'])
        require(pad['net'] == row['Net'] and math.isclose(float(row['X_mm']), pad['at'][0]-59, abs_tol=1e-5)
                and math.isclose(float(row['Y_mm']), 127.5-pad['at'][1], abs_tol=1e-5), 'Polarity coordinate mismatch')
    vip = read_json(VERIFY / 'VIA_IN_PAD_REVIEW.json')
    holes = csv_rows(PRODUCTION / 'pcb/VIPPO_HOLES.csv')
    require(vip['source_sha256'] == geometry['sha256'], 'VIPPO source mismatch')
    require(len(holes) == vip['unique_via_count'] == 27, 'VIPPO count mismatch')
    expected_holes = {r['via_id']: r for r in vip['intersections']}
    require({r['Via_ID'] for r in holes} == set(expected_holes), 'VIPPO coverage mismatch')
    for row in holes:
        x, y = expected_holes[row['Via_ID']]['via_xy']
        require(all(math.isclose(float(row[column]), expected, abs_tol=1e-5) for column, expected in [
            ('Gerber_X_mm', x), ('Gerber_Y_mm', -y), ('CPL_X_mm', x-59),
            ('CPL_Y_mm', 127.5-y), ('Drill_mm', expected_holes[row['Via_ID']]['drill_mm'])]), 'VIPPO coordinate mismatch')
    require((PRODUCTION / 'assembly/ASSEMBLY_TOP_BOTTOM.pdf').read_bytes().startswith(b'%PDF-'), 'Invalid assembly drawing')
    actual = {str(p.relative_to(PRODUCTION)) for p in PRODUCTION.rglob('*') if p.is_file()}
    require(actual - {'SHA256.json'} == PAYLOAD, 'Unexpected/missing production files: ' + str(actual ^ PAYLOAD))
    return {'pass': True, 'delivery_revision': VERSION, 'pcb_sha256': geometry['sha256'],
            'schematic_sha256': erc['source_hashes']['power_pd_3x5v_r3.xh.kicad_sch'],
            'project_rules_sha256': drc['project_rules_sha256'], 'SMT': 82, 'Top': 31, 'Bottom': 51,
            'THT': 4, 'MPN': 31, 'gerber_files': 14, 'copper_layers': 4,
            'VIPPO_candidates': 27, 'drc_violations': 0, 'unconnected': 0, 'erc_violations': 0}


def package():
    summary = verify_data()
    manifest = {**summary, 'files': {name: digest((PRODUCTION / name).read_bytes()) for name in sorted(PAYLOAD)}}
    write_json(PRODUCTION / 'SHA256.json', manifest)
    ARCHIVE.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(ARCHIVE, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(PAYLOAD | {'SHA256.json'}):
            archive.writestr(name, (PRODUCTION / name).read_bytes())
    write_json(VERIFY / 'PRODUCTION_VALIDATION.json', {**summary, 'archive_sha256': digest(ARCHIVE.read_bytes())})


def verify_archive():
    summary = verify_data()
    manifest = read_json(PRODUCTION / 'SHA256.json')
    require(set(manifest['files']) == PAYLOAD, 'Manifest coverage mismatch')
    require(all(manifest.get(key) == value for key, value in summary.items()), 'Manifest release identity mismatch')
    with zipfile.ZipFile(ARCHIVE) as archive:
        require(archive.testzip() is None and len(archive.namelist()) == len(PAYLOAD)+1, 'Production ZIP invalid')
        require(set(archive.namelist()) == PAYLOAD | {'SHA256.json'}, 'Production ZIP has extra/missing files')
        require(archive.read('SHA256.json') == (PRODUCTION / 'SHA256.json').read_bytes(), 'Manifest mismatch')
        for name, expected in manifest['files'].items():
            require(digest((PRODUCTION / name).read_bytes()) == digest(archive.read(name)) == expected, 'Delivery hash mismatch: ' + name)
    return {**summary, 'production_files': len(PAYLOAD)+1, 'archive_bytes': ARCHIVE.stat().st_size,
            'archive_sha256': digest(ARCHIVE.read_bytes())}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--import-legacy', type=Path, help='One-time import from the verified unpacked ERC release')
    parser.add_argument('--package', action='store_true', help='Repackage frozen production exports')
    args = parser.parse_args()
    if args.import_legacy:
        import_legacy(args.import_legacy.resolve())
    if args.package:
        package()
    print(json.dumps(verify_archive(), ensure_ascii=False, indent=2))
