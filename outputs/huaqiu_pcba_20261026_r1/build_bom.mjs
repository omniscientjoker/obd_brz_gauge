import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { SpreadsheetFile, Workbook } from '@oai/artifact-tool';

const out = path.dirname(fileURLToPath(import.meta.url));
const source = path.resolve(out, '../../hardware/electronics/power_pd_3x5v_r3/production/assembly/BOM_SMT.csv');
const workbook = await Workbook.fromCSV((await fs.readFile(source, 'utf8')).replace(/^\uFEFF/, ''), { sheetName: 'SMT_BOM' });
const sheet = workbook.worksheets.getItem('SMT_BOM');
const range = sheet.getUsedRange();
const values = range.values.filter(row => row.some(value => value !== null && value !== ''));
const expected = ['序号', '产品分类', '品牌', '型号*', '规格参数描述*', '封装*', '位号*', '单机用量*', '备注'];
if (JSON.stringify(values[0]) !== JSON.stringify(expected)) throw new Error('BOM header mismatch');
for (let row = 1; row < values.length; row++) {
  values[row][0] = Number(values[row][0]);
  values[row][7] = Number(values[row][7]);
}
if (values.slice(1).reduce((n, row) => n + row[7], 0) !== 82) throw new Error('BOM quantity mismatch');
range.clear({ applyTo: 'contents' });
sheet.getRangeByIndexes(0, 0, values.length, 9).values = values;
sheet.showGridLines = false;
sheet.freezePanes.freezeRows(1);
sheet.freezePanes.freezeColumns(4);
range.format.font = { name: 'Arial', size: 10, color: '#172334' };
range.format.wrapText = true;
range.format.verticalAlignment = 'top';
range.format.horizontalAlignment = 'left';
const widths = [55, 100, 160, 250, 420, 265, 225, 100, 500];
for (let col = 0; col < widths.length; col++) sheet.getRangeByIndexes(0, col, values.length, 1).format.columnWidthPx = widths[col];
sheet.getRange('A1:I1').format = {
  fill: '#233E5B', font: { name: 'Arial', size: 10, bold: true, color: '#FFFFFF' },
  horizontalAlignment: 'center', verticalAlignment: 'center', rowHeightPx: 34, wrapText: true,
};
for (let row = 1; row < values.length; row++) {
  const lines = values[row].map((value, col) => {
    const weighted = [...String(value)].reduce((n, character) => n + (character.charCodeAt(0) > 255 ? 13 : 7), 0);
    return Math.ceil(weighted / (widths[col] - 14));
  });
  const rowRange = sheet.getRangeByIndexes(row, 0, 1, 9);
  rowRange.format.rowHeightPx = Math.max(48, Math.max(...lines) * 16 + 14);
  if (row % 2 === 0) rowRange.format.fill = '#F0F4F8';
}
sheet.getRange(`A2:A${values.length}`).setNumberFormat('0');
sheet.getRange(`H2:H${values.length}`).setNumberFormat('0');
sheet.getRange(`A2:A${values.length}`).format.horizontalAlignment = 'right';
sheet.getRange(`H2:H${values.length}`).format.horizontalAlignment = 'right';
workbook.recalculate();
const inspected = await workbook.inspect({ kind: 'table', range: 'SMT_BOM!A1:I5', include: 'values,formulas', tableMaxRows: 5, tableMaxCols: 9, maxChars: 2000 });
await fs.writeFile(path.join(out, 'bom_inspection.ndjson'), inspected.ndjson);
const errors = await workbook.inspect({ kind: 'match', searchTerm: '#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!', options: { useRegex: true, maxResults: 20 }, maxChars: 1000 });
await fs.writeFile(path.join(out, 'bom_formula_errors.ndjson'), errors.ndjson);
for (const [name, bounds] of [['bom_top', 'A1:I7'], ['bom_bottom', `A25:I${values.length}`]]) {
  const image = await workbook.render({ sheetName: 'SMT_BOM', range: bounds, scale: 1.2, format: 'png' });
  await fs.writeFile(path.join(out, name + '.png'), new Uint8Array(await image.arrayBuffer()));
}
const output = await SpreadsheetFile.exportXlsx(workbook);
await output.save(path.join(out, 'BOM_HUAQIU_SMT.xlsx'));
console.log(JSON.stringify({ sheets: 1, BOM_rows: values.length - 1, unique_MPN: new Set(values.slice(1).map(row => row[2] + ':' + row[3])).size, parts: 82, output: path.join(out, 'BOM_HUAQIU_SMT.xlsx') }));
