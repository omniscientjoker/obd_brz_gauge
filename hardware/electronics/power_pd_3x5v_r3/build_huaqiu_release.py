#!/usr/bin/env python3
"""Build and check the Huaqiu production handoff from the frozen verified exports."""
import argparse
import csv
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

import openpyxl  # Read exported XLSX only; authoring is through Artifact Tool.
from pypdf import PdfReader
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak

import production_release as base

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
OUTPUT = REPO / 'outputs/huaqiu_pcba_20261026_r1'
DEST = ROOT / 'huaqiu'
ARCHIVE = ROOT / 'release/huaqiu-pcba-20261026-r1.zip'
VERSION = 'R3-HUAQIU-20261026-R1'
COPIES = {
    'production/PCB_GERBER.zip': '01_PCB/PCB_GERBER.zip',
    'production/pcb/VIPPO_HOLES.csv': '01_PCB/VIPPO_HOLES.csv',
    'production/assembly/BOM_SMT.csv': '02_SMT/BOM_HUAQIU_SMT.csv',
    'production/assembly/CPL_SMT.csv': '02_SMT/CPL_SMT.csv',
    'production/assembly/BOM_THT.csv': '03_THT/BOM_HUAQIU_THT.csv',
    'production/assembly/THT_POSITION.csv': '03_THT/THT_POSITION.csv',
    'production/assembly/ASSEMBLY_TOP_BOTTOM.pdf': '04_ASSEMBLY/ASSEMBLY_TOP_BOTTOM.pdf',
    'production/assembly/PIN1_POLARITY.csv': '04_ASSEMBLY/PIN1_POLARITY.csv',
    'production/assembly/BOM_ALL.csv': '04_ASSEMBLY/BOM_HUAQIU_ALL.csv',
    'verification/SCHEMATIC.pdf': '04_ASSEMBLY/SCHEMATIC.pdf',
}
EXTRA = {'README_HUAQIU.md', 'HUAQIU_ORDER_REMARKS.txt', 'HUAQIU_ORDER_GUIDE.pdf',
         'ORDER_PARAMETERS.md', 'ASSEMBLY_NOTES.md', '05_TEST/FIRST_ARTICLE_TEST.md',
         '02_SMT/BOM_HUAQIU_SMT.xlsx', '02_SMT/CPL_TOP.csv', '02_SMT/CPL_BOTTOM.csv'}
PAYLOAD = set(COPIES.values()) | EXTRA
HEADERS = ['序号', '产品分类', '品牌', '型号*', '规格参数描述*', '封装*', '位号*', '单机用量*', '备注']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def pdf_guide():
    pdfmetrics.registerFont(TTFont('HuaqiuSans', '/System/Library/Fonts/Supplemental/Arial Unicode.ttf'))
    body = ParagraphStyle('Body', fontName='HuaqiuSans', fontSize=9, leading=14,
                          textColor=colors.HexColor('#1F2937'), spaceAfter=7, wordWrap='CJK')
    title = ParagraphStyle('Title', parent=body, fontSize=18, leading=24, spaceAfter=12)
    sub = ParagraphStyle('Sub', parent=body, fontSize=12, leading=17, spaceBefore=10, spaceAfter=8)
    small = ParagraphStyle('Small', parent=body, fontSize=8, leading=11, spaceAfter=4)
    table_body = ParagraphStyle('TableBody', parent=body, fontSize=8, leading=11, spaceAfter=0)
    table_head = ParagraphStyle('TableHead', parent=table_body, textColor=colors.white)
    story = []

    def p(text, style=body):
        return Paragraph(escape(text.replace('`', '')).replace('\n', '<br/>'), style)

    def table(rows, widths, heading=True):
        cells = [[p(value, table_head if i == 0 and heading else table_body) for value in row] for i, row in enumerate(rows)]
        item = Table(cells, colWidths=widths, repeatRows=1 if heading else 0, hAlign='LEFT')
        rules = [('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 7),
                 ('RIGHTPADDING', (0, 0), (-1, -1), 7), ('TOPPADDING', (0, 0), (-1, -1), 5),
                 ('BOTTOMPADDING', (0, 0), (-1, -1), 5),
                 ('LINEBELOW', (0, 0), (-1, 0), 0.5, colors.HexColor('#CBD5E1'))]
        if heading:
            rules.append(('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#233E5B')))
        for i in range(1, len(rows)):
            if i % 2 == 0:
                rules.append(('BACKGROUND', (0, i), (-1, i), colors.HexColor('#F1F5F9')))
        item.setStyle(TableStyle(rules))
        return item

    story += [p('华秋 PCB + PCBA 打样下单说明', title),
              p(VERSION + ' / 2026-10-26', small),
              p('4层 / 82×55.8×1.2mm / 双面SMT82件 + THT后焊4件 / 31种MPN'),
              p('1. 上传文件与订单范围', sub)]
    story.append(table([
        ['环节', '文件与操作'],
        ['PCB裸板', '01_PCB/PCB_GERBER.zip，上传此内部ZIP；按第2页参数选择并确认VIPPO。'],
        ['双面SMT', '02_SMT/BOM_HUAQIU_SMT.xlsx 或同名CSV二选一，配套CPL_SMT.csv；单板82件，Top31 / Bottom51。'],
        ['分面坐标', '如平台要求，改用CPL_TOP.csv + CPL_BOTTOM.csv；不要与合并CPL重复导入。'],
        ['通孔后焊', '03_THT/BOM_HUAQIU_THT.csv + THT_POSITION.csv；4件连接器须纳入代采及后焊报价。'],
        ['工艺附件', '本PDF、04_ASSEMBLY/ASSEMBLY_TOP_BOTTOM.pdf、PIN1_POLARITY.csv、01_PCB/VIPPO_HOLES.csv，以及整包。'],
        ['总BOM备选', '04_ASSEMBLY/BOM_HUAQIU_ALL.csv 是86件总料表，可替代拆分BOM；不重复计量。'],
        ['订单备注', 'HUAQIU_ORDER_REMARKS.txt 可直接复制。板数由下单人填写，BOM保持单板用量。'],
    ], [80, 435]))
    story += [Spacer(1, 10), p('物料与工艺确认', sub),
              p('按厂家及完整MPN核价。无库存料、采购余量、工艺边/Mark、元件库零度和后焊服务由华秋接单时确认。下单资料没有虚构商城编号，未完成在线试导入或付款。'),
              p('华秋官方BOM模板下载核对日期：2026-10-26。九列表头、第一张工作表要求已核对。模板：https://smt.hqchip.com/uploads/bom.xls', small)]
    story += [PageBreak(), p('2. PCB与装配下单参数', title)]
    rows = []
    notes = []
    for line in (DEST / 'ORDER_PARAMETERS.md').read_text(encoding='utf-8').splitlines():
        if line.startswith('|') and '---' not in line:
            row = [part.strip() for part in line.strip('|').split('|')]
            if len(row) == 2:
                rows.append(row)
        elif line.startswith('VIPPO列表') or line.startswith('工厂不能'):
            notes.append(line)
    story.append(table(rows, [95, 420]))
    story += [Spacer(1, 8)] + [p(text, small) for text in notes]
    story += [PageBreak(), p('3. 装配、极性与坐标', title)]
    for line in (DEST / 'ASSEMBLY_NOTES.md').read_text(encoding='utf-8').splitlines():
        if line.startswith('- '):
            story.append(p(line[2:]))
    story += [p('坐标换算', sub), p('CPL：X = KiCadX - 59，Y = 127.5 - KiCadY，单位mm，Top/Bottom统一顶视。Gerber/钻孔：X = KiCadX，Y = -KiCadY。VIPPO列表同时给出两套坐标。'),
              p('PIN1_POLARITY给出焊盘中心，CPL给出封装原点。工厂按料号及卷带/元件库核对贴装角度、中心偏移，不能把极性表当作贴装坐标。')]
    story += [PageBreak(), p('4. 首板验收与记录', title)]
    for line in (DEST / '05_TEST/FIRST_ARTICLE_TEST.md').read_text(encoding='utf-8').splitlines():
        if re.match(r'^\d+\.', line):
            story.append(p(line))
    story += [p('测试交付', sub), p('实际输入PDO/电压、各路空载和带载电压、三路同时1.5A电流、热插拔/短路恢复、纹波与阶跃波形、封闭底盒温度及线长，应随首板记录。波形和温度验收条件在委托测试前确定。'),
              p('当前未完成实物带载、瞬态或车内高温资格验证。部分X5R MLCC最高85°C，输出保护典型2.03A是限流阈值，不是保证的2A峰值供电能力。'),
              p('输出仅接仪表USB-C供电口，禁止把5V接到3.7V电池接口。', small)]

    def footer(canvas, doc):
        canvas.saveState()
        canvas.setStrokeColor(colors.HexColor('#CBD5E1'))
        canvas.line(40, 32, A4[0]-40, 32)
        canvas.setFont('HuaqiuSans', 8)
        canvas.setFillColor(colors.HexColor('#64748B'))
        canvas.drawString(40, 20, VERSION)
        canvas.drawRightString(A4[0]-40, 20, str(doc.page))
        canvas.restoreState()

    SimpleDocTemplate(str(DEST / 'HUAQIU_ORDER_GUIDE.pdf'), pagesize=A4, leftMargin=40,
                      rightMargin=40, topMargin=36, bottomMargin=44,
                      title='华秋 PCB + PCBA 打样下单说明', author='Gauge Power').build(story, onFirstPage=footer, onLaterPages=footer)


def verify():
    summary = base.verify_archive()
    for source, target in COPIES.items():
        base.require(sha(ROOT / source) == sha(DEST / target), 'Changed manufacturing export: ' + target)
    book = openpyxl.load_workbook(DEST / '02_SMT/BOM_HUAQIU_SMT.xlsx', read_only=True, data_only=False)
    base.require(book.sheetnames == ['SMT_BOM'], 'Expected first/only BOM worksheet')
    values = list(book.active.values)
    expected = list(csv.reader((DEST / '02_SMT/BOM_HUAQIU_SMT.csv').open(encoding='utf-8-sig', newline='')))
    base.require(len(values) == len(expected) and list(values[0]) == HEADERS, 'Excel BOM shape/header mismatch')
    for actual, row in zip(values[1:], expected[1:]):
        normalized = list(row)
        normalized[0], normalized[7] = int(row[0]), int(row[7])
        base.require(list(actual) == normalized, 'Excel/CSV BOM value mismatch')
    base.require(sum(row[7] for row in values[1:]) == 82, 'Expected 82 SMT parts')
    book.close()
    combined = base.csv_rows(DEST / '02_SMT/CPL_SMT.csv')
    for side in ['Top', 'Bottom']:
        rows = base.csv_rows(DEST / ('02_SMT/CPL_' + side.upper() + '.csv'))
        base.require(rows == [row for row in combined if row['Layer'] == side], 'Split CPL differs')
    base.require(len(PdfReader(DEST / 'HUAQIU_ORDER_GUIDE.pdf').pages) == 4, 'Guide must fit the reviewed 4-page layout')
    actual = {str(path.relative_to(DEST)) for path in DEST.rglob('*') if path.is_file()}
    base.require(actual == PAYLOAD | {'SHA256.json'}, 'Unexpected or missing Huaqiu delivery files')
    manifest = base.read_json(DEST / 'SHA256.json')
    base.require(set(manifest['files']) == PAYLOAD, 'Huaqiu manifest mismatch')
    with zipfile.ZipFile(ARCHIVE) as archive:
        base.require(archive.testzip() is None and len(archive.namelist()) == len(PAYLOAD)+1 and set(archive.namelist()) == actual, 'Huaqiu ZIP invalid')
        for name, expected_sha in manifest['files'].items():
            base.require(sha(DEST / name) == hashlib.sha256(archive.read(name)).hexdigest() == expected_sha, 'Huaqiu file hash mismatch: ' + name)
        base.require(archive.read('SHA256.json') == (DEST / 'SHA256.json').read_bytes(), 'Huaqiu manifest copy differs')
    return {**summary, 'delivery_revision': VERSION, 'production_files': len(actual),
            'archive_bytes': ARCHIVE.stat().st_size, 'archive_sha256': sha(ARCHIVE),
            'guide_pages': 4, 'xlsx_csv_match': True, 'split_cpl_verified': True,
            'online_import_verified': False, 'supplier_stock_verified': False}


def build():
    summary = base.verify_archive()
    for source, target in COPIES.items():
        destination = DEST / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / source, destination)
    shutil.copy2(OUTPUT / 'BOM_HUAQIU_SMT.xlsx', DEST / '02_SMT/BOM_HUAQIU_SMT.xlsx')
    for source, target in [('ORDER_PARAMETERS.md', 'ORDER_PARAMETERS.md'),
                           ('ASSEMBLY_NOTES.md', 'ASSEMBLY_NOTES.md'),
                           ('FIRST_ARTICLE_TEST.md', '05_TEST/FIRST_ARTICLE_TEST.md')]:
        text = (base.PRODUCTION / source).read_text(encoding='utf-8')
        changes = {base.VERSION: VERSION, '`PCB_GERBER.zip`': '`01_PCB/PCB_GERBER.zip`',
                   'pcb/VIPPO_HOLES.csv': '01_PCB/VIPPO_HOLES.csv',
                   '`BOM_SMT.csv`': '`02_SMT/BOM_HUAQIU_SMT.csv`',
                   '`BOM_THT.csv`': '`03_THT/BOM_HUAQIU_THT.csv`',
                   '`BOM_ALL.csv`': '`04_ASSEMBLY/BOM_HUAQIU_ALL.csv`',
                   '`PIN1_POLARITY.csv`': '`04_ASSEMBLY/PIN1_POLARITY.csv`',
                   '`FIRST_ARTICLE_TEST.md`': '`05_TEST/FIRST_ARTICLE_TEST.md`'}
        for old, new in changes.items():
            text = text.replace(old, new)
        destination = DEST / target
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(text, encoding='utf-8')
    with (DEST / '02_SMT/CPL_SMT.csv').open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        fields, rows = reader.fieldnames, list(reader)
    for side in ['Top', 'Bottom']:
        with (DEST / ('02_SMT/CPL_' + side.upper() + '.csv')).open('w', encoding='utf-8-sig', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            writer.writerows(row for row in rows if row['Layer'] == side)
    pdf_guide()
    template = OUTPUT / 'huaqiu_official_bom_template.xls'
    manifest = {**summary, 'delivery_revision': VERSION,
                'source_delivery_revision': base.VERSION,
                'official_bom_template_url': 'https://smt.hqchip.com/uploads/bom.xls',
                'official_bom_template_sha256': sha(template),
                'official_bom_template_checked_on': '2026-10-26',
                'online_import_verified': False, 'supplier_stock_verified': False,
                'files': {name: sha(DEST / name) for name in sorted(PAYLOAD)}}
    for key in ['production_files', 'archive_bytes', 'archive_sha256']:
        manifest.pop(key, None)
    write_json(DEST / 'SHA256.json', manifest)
    with zipfile.ZipFile(ARCHIVE, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(PAYLOAD | {'SHA256.json'}):
            archive.write(DEST / name, name)
    result = verify()
    write_json(ROOT / 'verification/HUAQIU_PRODUCTION_VALIDATION.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    print(json.dumps(verify() if args.verify_only else build(), ensure_ascii=False, indent=2))
