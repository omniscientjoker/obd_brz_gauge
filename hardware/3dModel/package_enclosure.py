#!/usr/bin/env python3
"""Package the validated printable enclosure, not electrical fabrication files."""
from pathlib import Path
import hashlib
import json
import re
import zipfile

ROOT = Path(__file__).resolve().parent
SOURCES = [ROOT / name for name in (
    'params.scad', 'parts.scad', 'panel.scad', 'base.scad', 'carrier_a.scad',
    'carrier_b.scad', 'fit_template.scad', 'fit_template_b.scad',
    'power_board_mockup.scad',
    'assembly.scad', 'assembly_b.scad', 'validate.mjs', 'preview.py',
    'package_enclosure.py', 'README.md', 'USB_FLUSH_WALL.zh-CN.md', 'preview.png', 'carrier_detail.png', 'base_detail.png',
    'electronics/export_mechanical.mjs', 'electronics/DESIGN.zh-CN.md',
    'electronics/placement.png',
    'electronics/power_board_outline.dxf', 'electronics/power_board_interface.json',
    'electronics/power_pd_3x5v_r3/mechanical_interface.json',
)]
REPORT = ROOT / 'stl/validation.json'
checks = json.loads(REPORT.read_text())
expected = {'panel', 'base', 'carrier_a', 'carrier_b', 'fit_template', 'fit_template_b',
            'power_board_mockup', 'interference',
            'interference_b', 'electronics_interference', 'mixed_interference',
            'usb_interference', 'power_usb_interference'}
if {entry['name'] for entry in checks} != expected:
    raise RuntimeError('Incomplete validation report')
latest_source = max(path.stat().st_mtime_ns for path in ROOT.glob('*.scad'))
if REPORT.stat().st_mtime_ns < latest_source:
    raise RuntimeError('CAD validation predates the source')
for check in checks:
    if check['name'] in ('panel', 'base'):
        if check.get('outerOutline') != 'all mesh vertices inside capsule silhouette':
            raise RuntimeError(f"Missing enclosure outline check: {check['name']}")
    if check['name'].endswith('interference') or check['name'] == 'interference_b':
        if check.get('assembledInterference') != 'no positive-volume overlap':
            raise RuntimeError(f"Interference check failed: {check['name']}")
    elif check.get('connectedSolids') != 1:
        raise RuntimeError(f"Invalid printable solid: {check['name']}")
    else:
        SOURCES.append(ROOT / 'stl' / (check['name']+'.stl'))
SOURCES.append(REPORT)
for path in SOURCES:
    if not path.is_file():
        raise FileNotFoundError(path)
target = ROOT / 'three_gauge_v2.zip'
included = {path.resolve() for path in SOURCES}
with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
    packaged = {}
    for path in SOURCES:
        relative = str(path.relative_to(ROOT))
        data = path.read_bytes()
        if path.suffix == '.md':
            # PCB is a separate package. Keep external-package paths as text
            # instead of shipping broken links in the enclosure-only archive.
            def local_link(match):
                label, link = match.groups()
                if '://' in link or (path.parent / link).resolve() in included:
                    return match.group(0)
                return f'{label}（`{link}`，见V2工作目录或独立PCB资料）'
            data = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', local_link, data.decode()).encode()
        packaged[relative] = data
        archive.writestr('three_gauge_v2/'+relative, data)
    manifest = {name: hashlib.sha256(data).hexdigest() for name, data in packaged.items()}
    archive.writestr('three_gauge_v2/SHA256.json', json.dumps(manifest, indent=2)+'\n')
with zipfile.ZipFile(target) as archive:
    if archive.testzip() is not None:
        raise RuntimeError('Invalid ZIP')
print(target)
