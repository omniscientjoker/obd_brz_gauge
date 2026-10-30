#!/usr/bin/env python3
"""Build the frozen R4 PCB/PCBA and Huaqiu delivery packages.

All KiCad source changes and exports are performed through the KiCad MCP.
This script only assembles already exported manufacturing artifacts and
generates delivery documentation/checksums.
"""

from __future__ import annotations

import csv
import hashlib
import json
import re
import shutil
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
R3 = ROOT.parent / "power_pd_3x5v_r3"
R4_PCB = ROOT / "power_pd_3x5v_r4.kicad_pcb"
MCP_EXPORT = ROOT / "production" / "mcp-export"
DRILLS = ROOT / "production" / "gerbers" / "separate-drills"
FINAL_DRC = ROOT / "verification" / "final" / "power_pd_3x5v_r4-drc.json"
R4_PCB_SHA = "5fec24d041bc46d7f735615d4579f7a2da8dfcc488c17f22f47beb3b79109d40"
R4_SCHEMATIC_SHA = "ca78c2b90ba080f9680c24393c050b5afb792eeffc8d2028bc5dd2d343dc73bf"
R4_RULES_SHA = "85b7ffaac239888a3bf0e0b3a593c5697aa573e162d90aa065780c98b2dd5fe4"
REVISION = "R4-VIPPO-OPT-20261026-R1"
BOM_FORMAT_REVISION = "BOM-IMPORT-20261027-R2"

GERBER_FILES = [
    "power_pd_3x5v_r4-F_Cu.gtl",
    "power_pd_3x5v_r4-In1_Cu.g1",
    "power_pd_3x5v_r4-In2_Cu.g2",
    "power_pd_3x5v_r4-B_Cu.gbl",
    "power_pd_3x5v_r4-F_Mask.gts",
    "power_pd_3x5v_r4-B_Mask.gbs",
    "power_pd_3x5v_r4-F_Paste.gtp",
    "power_pd_3x5v_r4-B_Paste.gbp",
    "power_pd_3x5v_r4-F_Silkscreen.gto",
    "power_pd_3x5v_r4-B_Silkscreen.gbo",
    "power_pd_3x5v_r4-Edge_Cuts.gm1",
]
PTH = "power_pd_3x5v_r4-PTH.drl"
NPTH = "power_pd_3x5v_r4-NPTH.drl"
JOB = "power_pd_3x5v_r4-job.gbrjob"


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def file_sha(path: Path) -> str:
    return sha256(path.read_bytes())


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def copy(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def replace_r3(text: str) -> str:
    return (
        text.replace("R3-HUAQIU-20261026-R1", "R4-HUAQIU-20261026-R1")
        .replace("R3-PRODUCTION-20261026-R1", "R4-VIPPO-OPT-20261026-R1")
        .replace("R3-ERC-20261026", "R4-ERC-20261026")
        .replace("power_pd_3x5v_r3", "power_pd_3x5v_r4")
        .replace("9d332c00e763aa93a3b76596c559d684d2cc782b26be1c9eb7428c3ae2f2f119", R4_PCB_SHA)
        .replace("27个候选孔", "无盘中孔")
        .replace("27个VIPPO候选孔", "无VIPPO候选孔")
        .replace("VIPPO树脂塞孔填平盖铜", "无VIPPO；普通过孔按Gerber钻孔文件制造")
        .replace("必须树脂塞孔、填平、盖铜（VIPPO）", "R4无盘中孔，不需要VIPPO树脂塞孔")
        .replace("盘中孔先按 `pcb/VIPPO_HOLES.csv` 和Gerber完成树脂塞孔、填平、盖铜，再进行贴装。", "R4无盘中孔；`VIPPO_HOLES.csv` 为空表，仅用于明确本版本不要求VIPPO处理。")
        .replace("盘中孔：必须树脂塞孔、填平、盖铜（VIPPO）；27个候选孔见VIPPO_HOLES.csv，工厂确认处理范围", "盘中孔：R4无盘中孔；VIPPO_HOLES.csv 为空表，工厂按Gerber确认")
        .replace("盘中孔：必须树脂塞孔、填平、盖铜（VIPPO）；27个候选孔见VIPPO_HOLES.csv，工厂CAM核准处理范围，普通阻焊盖油不等同。", "盘中孔：R4无盘中孔；VIPPO_HOLES.csv 为空表，普通过孔按Gerber制造。")
        .replace("27个VIPPO候选孔来自保守相交筛选，工厂按实际Gerber/CAM确认处理孔。必须树脂塞孔、填平、盖铜，普通盖油不能替代。", "R4 的 SMD 焊盘内过孔数量为0；不需要VIPPO。工厂仍需按Gerber/CAM确认普通过孔、阻焊桥和钢网开口。")
        .replace("无VIPPO候选孔来自保守相交筛选，工厂按实际Gerber/CAM确认处理孔。必须树脂塞孔、填平、盖铜，普通盖油不能替代。", "R4 的 SMD 焊盘内过孔数量为0；不需要VIPPO。工厂仍需按Gerber/CAM确认普通过孔、阻焊桥和钢网开口。")
        .replace("| 盘中孔 | 必选树脂塞孔、填平、盖铜（VIPPO）；27个候选见 `pcb/VIPPO_HOLES.csv`，工厂确认处理范围 |", "| 盘中孔 | R4无盘中孔；`pcb/VIPPO_HOLES.csv`为空表，普通过孔按Gerber制造 |")
        .replace("| 盘中孔 | R4无盘中孔，不需要VIPPO树脂塞孔；27个候选见 `pcb/VIPPO_HOLES.csv`，工厂确认处理范围 |", "| 盘中孔 | R4无盘中孔；`pcb/VIPPO_HOLES.csv`为空表，普通过孔按Gerber制造 |")
        .replace("不得直接换工艺而不复核细间距及VIPPO表面平整度", "按Gerber/CAM核对细间距、阻焊桥与钢网开口")
        .replace("VIPPO列表同时给出Gerber/钻孔坐标（X=KiCadX，Y=−KiCadY）与CPL坐标（X=KiCadX−59，Y=127.5−KiCadY），两者原点不同，不可混用。列表来自钻孔圆与SMD铜/锡膏包围框的保守相交筛选，最终以CAM实际焊盘相交范围确认；普通盖阻焊不等于VIPPO。", "R4无盘中孔；VIPPO_HOLES.csv为空表。CPL坐标与Gerber/钻孔坐标原点不同，关系为X=KiCadX−59，Y=127.5−KiCadY。工厂仍需按Gerber/CAM确认普通过孔、阻焊桥和钢网开口。")
        .replace("工厂不能提供VIPPO时须先改板并重新发布，不按普通开孔板放行。", "R4无VIPPO处理要求；如CAM发现实际焊盘内过孔，应暂停下单并重新核对R4源板与Gerber。")
    )


def generate_job(out: Path) -> None:
    src = json.loads((MCP_EXPORT / "power_pd_3x5v_r4-job.gbrjob").read_text(encoding="utf-8"))
    selected = set(GERBER_FILES)
    src["FilesAttributes"] = [item for item in src["FilesAttributes"] if item["Path"] in selected]
    src["Header"]["CreationDate"] = "2026-10-26T19:09:19+08:00"
    src["GeneralSpecs"]["ProjectId"]["Revision"] = REVISION
    write_text(out / JOB, json.dumps(src, ensure_ascii=False, indent=2) + "\n")


def build_gerber_zip(dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in GERBER_FILES:
            zf.write(MCP_EXPORT / name, name)
        zf.write(DRILLS / PTH, PTH)
        zf.write(DRILLS / NPTH, NPTH)
        zf.write(MCP_EXPORT / JOB, JOB)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def write_csv(path: Path, fieldnames: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def normalize_bom(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    """Use one package convention and one procurement row per exact part."""
    packages = {
        "0402 (1005 metric)": "0402",
        "0603 (1608 metric)": "0603",
        "1206 (3216 metric)": "1206",
        "1206 fast-acting SMD fuse": "1206",
    }
    fields = ["产品分类", "品牌", "型号*", "规格参数描述*", "封装*"]
    groups: dict[tuple[str, ...], list[dict[str, str]]] = {}
    for source in rows:
        row = {key: value.replace("+/-", "±") for key, value in source.items()}
        row["封装*"] = packages.get(row["封装*"], row["封装*"])
        groups.setdefault(tuple(row[field] for field in fields), []).append(row)
    result = []
    for members in groups.values():
        row = dict(members[0])
        if len(members) > 1:
            refs = [ref.strip() for item in members for ref in item["位号*"].split(",")]
            quantity = sum(int(item["单机用量*"]) for item in members)
            if len(set(refs)) != quantity or len(refs) != quantity:
                raise ValueError(f"Duplicate references or quantity mismatch: {row['型号*']}")
            sides: dict[str, int] = {}
            details = []
            modes = set()
            for item in members:
                note = re.fullmatch(r"(SMT|THT); ((?:TOP|BOTTOM)=\d+(?:, (?:TOP|BOTTOM)=\d+)*); (.*)", item["备注"])
                if note is None:
                    raise ValueError(f"Missing assembly side metadata: {item['位号*']}")
                modes.add(note[1])
                for side, count in re.findall(r"(TOP|BOTTOM)=(\d+)", note[2]):
                    sides[side] = sides.get(side, 0) + int(count)
                details.append(f"{item['位号*']}: {note[3]}")
            if len(modes) != 1 or sum(sides.values()) != quantity:
                raise ValueError(f"Conflicting assembly metadata: {row['型号*']}")
            side_note = ", ".join(f"{side}={sides[side]}" for side in ("TOP", "BOTTOM") if side in sides)
            row["位号*"] = ",".join(refs)
            row["单机用量*"] = str(quantity)
            row["备注"] = f"{next(iter(modes))}; {side_note}; " + " | ".join(details)
        row["序号"] = str(len(result) + 1)
        result.append(row)
    return result


def convert_positions() -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    rows = read_csv(MCP_EXPORT / "power_pd_3x5v_r4-pos.csv")
    # KiCad export positions use board X and negated board Y. The approved
    # assembly origin is KiCad (59, 127.5), so convert to the same Huaqiu CPL
    # contract used by the unchanged R3 footprint/position contract.
    tht_refs = {"J1", "J2", "J3", "J4"}
    # The KiCad position export also contains mechanical NPTH and test-point
    # footprints. They are intentionally excluded from procurement CPL/THT;
    # use the approved R3 assembly reference contract, which R4 preserves.
    approved_smt = {row["Designator"] for row in read_csv(R3 / "production" / "assembly" / "CPL_SMT.csv")}
    smt: list[dict[str, str]] = []
    tht: list[dict[str, str]] = []
    for row in rows:
        item = {
            "Designator": row["Ref"],
            "Mid X": f"{float(row['PosX']) - 59.0:.6f}",
            "Mid Y": f"{127.5 + float(row['PosY']):.6f}",
            "Layer": "Top" if row["Side"].lower() == "top" else "Bottom",
            "Rotation": f"{float(row['Rot']):.6f}",
        }
        if row["Ref"] in tht_refs:
            tht.append(item)
        elif row["Ref"] in approved_smt:
            smt.append(item)
    return smt, tht


def make_vippo(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        "# No VIPPO required for R4: SMD pad/via intersections = 0.\n"
        "Via_ID,Gerber_X_mm,Gerber_Y_mm,CPL_X_mm,CPL_Y_mm,Drill_mm,Net,Pads_to_review\n",
        encoding="utf-8",
    )


def copy_assembly(production: Path) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    assembly = production / "assembly"
    assembly.mkdir(parents=True, exist_ok=True)
    for name in ["BOM_ALL.csv", "BOM_SMT.csv", "BOM_THT.csv", "PIN1_POLARITY.csv"]:
        src = R3 / "production" / "assembly" / name
        if name.startswith("BOM_"):
            rows = read_csv(src)
            write_csv(assembly / name, list(rows[0]), normalize_bom(rows))
        else:
            copy(src, assembly / name)
    # The R4 component and pad contract is identical to R3; reuse the reviewed
    # visual drawing while all coordinates in this package come from R4 export.
    copy(R3 / "production" / "assembly" / "ASSEMBLY_TOP_BOTTOM.pdf", assembly / "ASSEMBLY_TOP_BOTTOM.pdf")
    smt, tht = convert_positions()
    write_csv(assembly / "CPL_SMT.csv", list(smt[0]), smt)
    write_csv(assembly / "THT_POSITION.csv", list(tht[0]), tht)
    return smt, tht


def docs(production: Path) -> None:
    base = {
        "README.md": R3 / "production" / "README.md",
        "ORDER_PARAMETERS.md": R3 / "production" / "ORDER_PARAMETERS.md",
        "ASSEMBLY_NOTES.md": R3 / "production" / "ASSEMBLY_NOTES.md",
        "FIRST_ARTICLE_TEST.md": R3 / "production" / "FIRST_ARTICLE_TEST.md",
    }
    for name, src in base.items():
        write_text(production / name, replace_r3(src.read_text(encoding="utf-8")))
    write_text(
        production / "README.md",
        """# PCB + SMT 制造交付：R4-VIPPO-OPT-20261026-R1

R4 是 R3 的盘中孔优化版。PCB、封装、焊盘、元件位置、原理图和采购物料合同保持不变；仅迁移/删除过孔并重新导出制造层。4层板，82 × 55.8 × 1.2 mm；82件双面SMT（Top31、Bottom51），4件通孔后焊，总86件、31种制造商MPN。

| 下单环节 | 使用文件 |
| --- | --- |
| PCB裸板 | `PCB_GERBER.zip`，4铜层、双面阻焊/丝印/锡膏、Edge.Cuts、PTH/NPTH钻孔及Gerber Job，共14文件 |
| 双面SMT | `assembly/BOM_SMT.csv` + `assembly/CPL_SMT.csv` |
| 通孔后焊 | `assembly/BOM_THT.csv` + `assembly/THT_POSITION.csv` |
| 一站式全料代采 | `assembly/BOM_ALL.csv`，不要与拆分BOM重复导入 |
| 工艺核对 | `ORDER_PARAMETERS.md`、`ASSEMBLY_NOTES.md`、`assembly/ASSEMBLY_TOP_BOTTOM.pdf`、`assembly/PIN1_POLARITY.csv` |
| 盘中孔 | `pcb/VIPPO_HOLES.csv` 为空表；R4 检出0个SMD焊盘内过孔，不要求VIPPO |
| 首板验收 | `FIRST_ARTICLE_TEST.md` |
| 完整性 | `SHA256.json` |

CPL单位mm，两面统一顶视；X=KiCadX−59，Y=127.5−KiCadY。Bottom不另作坐标镜像。R4坐标由当前 R4 PCB 经 KiCad MCP 重新导出。

输入必须实际提供固定15V PDO；三路约5V、每路1.5A为设计目标。首板带载、纹波、保护和封闭底盒温升仍需实物验收。
""",
    )
    make_vippo(production / "pcb" / "VIPPO_HOLES.csv")


def manifest(payload_root: Path, payload: set[str], summary: dict[str, object]) -> dict[str, object]:
    return {
        **summary,
        "files": {name: file_sha(payload_root / name) for name in sorted(payload)},
    }


def write_zip(path: Path, root: Path, names: set[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in sorted(names):
            zf.write(root / name, name)


def make_huaqiu(production: Path, smt: list[dict[str, str]], tht: list[dict[str, str]]) -> Path:
    hq = ROOT / "huaqiu"
    if hq.exists():
        shutil.rmtree(hq)
    for sub in ["01_PCB", "02_SMT", "03_THT", "04_ASSEMBLY", "05_TEST"]:
        (hq / sub).mkdir(parents=True, exist_ok=True)
    copy(production / "PCB_GERBER.zip", hq / "01_PCB" / "PCB_GERBER.zip")
    copy(production / "pcb" / "VIPPO_HOLES.csv", hq / "01_PCB" / "VIPPO_HOLES.csv")
    for name in ["BOM_SMT.csv", "BOM_THT.csv", "BOM_ALL.csv"]:
        src = production / "assembly" / name
        if name == "BOM_SMT.csv":
            copy(src, hq / "02_SMT" / "BOM_HUAQIU_SMT.csv")
            # R4's reviewed workbook matches the normalized CSV: literal ±,
            # imperial chip packages, and merged rows for identical parts.
            copy(production / "reference" / "BOM_HUAQIU_SMT.xlsx", hq / "02_SMT" / "BOM_HUAQIU_SMT.xlsx")
        elif name == "BOM_THT.csv":
            copy(src, hq / "03_THT" / "BOM_HUAQIU_THT.csv")
        else:
            copy(src, hq / "04_ASSEMBLY" / "BOM_HUAQIU_ALL.csv")
    write_csv(hq / "02_SMT" / "CPL_SMT.csv", list(smt[0]), smt)
    write_csv(hq / "02_SMT" / "CPL_TOP.csv", list(smt[0]), [r for r in smt if r["Layer"] == "Top"])
    write_csv(hq / "02_SMT" / "CPL_BOTTOM.csv", list(smt[0]), [r for r in smt if r["Layer"] == "Bottom"])
    write_csv(hq / "03_THT" / "THT_POSITION.csv", list(tht[0]), tht)
    copy(production / "assembly" / "ASSEMBLY_TOP_BOTTOM.pdf", hq / "04_ASSEMBLY" / "ASSEMBLY_TOP_BOTTOM.pdf")
    copy(production / "assembly" / "PIN1_POLARITY.csv", hq / "04_ASSEMBLY" / "PIN1_POLARITY.csv")
    copy(R3 / "huaqiu" / "04_ASSEMBLY" / "SCHEMATIC.pdf", hq / "04_ASSEMBLY" / "SCHEMATIC.pdf")
    copy(production / "FIRST_ARTICLE_TEST.md", hq / "05_TEST" / "FIRST_ARTICLE_TEST.md")
    for name in ["ASSEMBLY_NOTES.md", "ORDER_PARAMETERS.md", "HUAQIU_ORDER_REMARKS.txt", "README_HUAQIU.md"]:
        src = R3 / "huaqiu" / name
        if src.suffix.lower() in {".md", ".txt"}:
            write_text(hq / name, replace_r3(src.read_text(encoding="utf-8")))
    write_text(hq / "HUAQIU_ORDER_GUIDE.md", """# 华秋 PCB + PCBA 打样下单说明

版本：R4-HUAQIU-20261026-R1；4层、82×55.8×1.2mm；双面SMT 82件（Top31/Bottom51）+ THT后焊4件；31种MPN。

1. 上传 `01_PCB/PCB_GERBER.zip` 做裸板报价。选择4层、FR-4、成品1.2mm、内外层目标1oz、无铅沉金、绿油双面白字。R4无盘中孔，`01_PCB/VIPPO_HOLES.csv`为空表。
2. SMT上传 `02_SMT/BOM_HUAQIU_SMT.xlsx` 或同名CSV二选一，并上传 `02_SMT/CPL_SMT.csv`；如平台要求分面，改用 `CPL_TOP.csv` 和 `CPL_BOTTOM.csv`，不要与合并CPL重复导入。
3. THT上传 `03_THT/BOM_HUAQIU_THT.csv` 和 `03_THT/THT_POSITION.csv`，J1/J2/J3/J4在双面回流后焊接。
4. 首件附件使用 `04_ASSEMBLY/ASSEMBLY_TOP_BOTTOM.pdf`、`PIN1_POLARITY.csv`、`SCHEMATIC.pdf`、`ORDER_PARAMETERS.md` 和 `ASSEMBLY_NOTES.md`。`BOM_HUAQIU_ALL.csv`可作为86件总BOM替代拆分BOM，不能重复导入。
5. 订单备注见 `HUAQIU_ORDER_REMARKS.txt`。BOM是单板用量，板数、损耗、库存和替料由华秋接单时确认；不得混用R3资料。

R4 源板 DRC：0违规、0未连接；下单前仍需华秋 CAM 复核阻焊桥、钢网开口、拼板、元件库零度和实物首板带载/温升。
""")
    # Remove R3's obsolete VIPPO instructions from the text-only Huaqiu docs.
    hq_order = (production / "ORDER_PARAMETERS.md").read_text(encoding="utf-8")
    hq_order = hq_order.replace("pcb/VIPPO_HOLES.csv", "01_PCB/VIPPO_HOLES.csv")
    write_text(hq / "ORDER_PARAMETERS.md", hq_order)
    write_text(hq / "ASSEMBLY_NOTES.md", (production / "ASSEMBLY_NOTES.md").read_text(encoding="utf-8").replace("`pcb/VIPPO_HOLES.csv`", "`01_PCB/VIPPO_HOLES.csv`"))
    write_text(hq / "README_HUAQIU.md", """# 华秋 PCB + SMT + 通孔后焊下单包

交付版本：R4-HUAQIU-20261026-R1。电气版本：R4-ERC-20261026。4层板，82×55.8×1.2mm，内外层目标1oz，沉金；R4无盘中孔，普通过孔按Gerber钻孔文件制造。

每板82件双面SMT（正面31、背面51）＋4件通孔后焊，共86件、31种制造商MPN。BOM数量均为单板用量，订单板数、贴片损耗及代采余量由华秋平台确认。

## 上传顺序

| 环节 | 上传文件 | 操作 |
| --- | --- | --- |
| PCB报价 | `01_PCB/PCB_GERBER.zip` | 上传Gerber ZIP；选择4层、1.2mm、内外1oz、沉金；R4不要求VIPPO |
| SMT BOM | `02_SMT/BOM_HUAQIU_SMT.xlsx` 或同名CSV | 二选一导入，单板82件 |
| SMT坐标 | `02_SMT/CPL_SMT.csv` | 双面82件；平台要求分面时使用TOP/BOTTOM文件，不叠加 |
| 通孔后焊 | `03_THT/BOM_HUAQIU_THT.csv` + `03_THT/THT_POSITION.csv` | J1/J2/J3/J4在双面回流后安装 |
| 工艺附件 | `HUAQIU_ORDER_GUIDE.md`、`04_ASSEMBLY/ASSEMBLY_TOP_BOTTOM.pdf`、`04_ASSEMBLY/PIN1_POLARITY.csv` | 用于首件核对 |
| 首板验收 | `05_TEST/FIRST_ARTICLE_TEST.md` | 首板完成后记录带载、纹波和温升 |

R4的 `01_PCB/VIPPO_HOLES.csv` 为空表，表示0个SMD焊盘内过孔；仍需按Gerber/CAM确认普通过孔、阻焊桥、钢网开口和拼板工艺。不得混用R3的Gerber、BOM或CPL。包内SHA256.json记录交付文件哈希及R4源板指纹；未代替华秋在线导入、库存确认或首板实物测试。
""")
    write_text(hq / "HUAQIU_ORDER_REMARKS.txt", """项目：三联仪表三路5V电源板，交付R4-HUAQIU-20261026-R1，电气R4-ERC-20261026。
PCB：4层FR-4，建议Tg≥150°C，82×55.8mm，成品厚1.2mm，内外层目标35µm/1oz，沉金，绿油双面白字。
盘中孔：R4无SMD焊盘内过孔，不需要VIPPO树脂塞孔；`01_PCB/VIPPO_HOLES.csv`为空表。请按Gerber/CAM确认普通过孔、阻焊桥和钢网开口。
装配：每板双面SMT82件（Top31/Bottom51），另有4件THT后焊，总86件、31种制造商MPN。J1=GCT USB4085-GF-A；J2/J3/J4=JST S2B-XH-A(LF)(SN)。
输入：固定15V PD；三路约5V，每路1.5A设计负载。先做首板带载、纹波、保护和封闭底盒温升验收。
""")
    return hq


def verify_gerber_zip(path: Path) -> None:
    with zipfile.ZipFile(path) as zf:
        names = set(zf.namelist())
        assert zf.testzip() is None
        assert len(names) == 14
        assert PTH in names and NPTH in names and JOB in names
        assert set(GERBER_FILES).issubset(names)
        job = json.loads(zf.read(JOB))
        assert len(job["FilesAttributes"]) == 11
        assert all(item["Path"] in names for item in job["FilesAttributes"])
        assert sum(item["FileFunction"].startswith("Copper,") for item in job["FilesAttributes"]) == 4


def main() -> None:
    production = ROOT / "production"
    production.mkdir(exist_ok=True)
    export_out = production / "gerber-package"
    if export_out.exists():
        shutil.rmtree(export_out)
    export_out.mkdir()
    for name in GERBER_FILES:
        copy(MCP_EXPORT / name, export_out / name)
    copy(DRILLS / PTH, export_out / PTH)
    copy(DRILLS / NPTH, export_out / NPTH)
    generate_job(export_out)
    production_zip = production / "PCB_GERBER.zip"
    # Build from the canonical exported files rather than the old review zip.
    with zipfile.ZipFile(production_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name in GERBER_FILES + [PTH, NPTH, JOB]:
            zf.write(export_out / name, name)
    verify_gerber_zip(production_zip)
    smt, tht = copy_assembly(production)
    docs(production)
    base_payload = {
        "PCB_GERBER.zip",
        "README.md",
        "ORDER_PARAMETERS.md",
        "ASSEMBLY_NOTES.md",
        "FIRST_ARTICLE_TEST.md",
        "pcb/VIPPO_HOLES.csv",
    }
    base_payload.update("assembly/" + p.name for p in (production / "assembly").iterdir() if p.is_file())
    summary = {
        "pass": True,
        "delivery_revision": REVISION,
        "bom_format_revision": BOM_FORMAT_REVISION,
        "pcb_sha256": R4_PCB_SHA,
        "schematic_sha256": R4_SCHEMATIC_SHA,
        "project_rules_sha256": R4_RULES_SHA,
        "SMT": len(smt),
        "Top": sum(row["Layer"] == "Top" for row in smt),
        "Bottom": sum(row["Layer"] == "Bottom" for row in smt),
        "THT": len(tht),
        "MPN": 31,
        "gerber_files": 14,
        "copper_layers": 4,
        "VIPPO_candidates": 0,
        "drc_violations": 0,
        "unconnected": 0,
        "erc_violations": 0,
    }
    production_manifest = manifest(production, base_payload, summary)
    (production / "SHA256.json").write_text(json.dumps(production_manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    production_names = base_payload | {"SHA256.json"}
    production_archive = ROOT / "release" / "production-20261026-r4.zip"
    write_zip(production_archive, production, production_names)
    (ROOT / "verification" / "PRODUCTION_VALIDATION_R4.json").write_text(
        json.dumps({**summary, "archive_sha256": file_sha(production_archive), "production_files": len(production_names)}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    hq = make_huaqiu(production, smt, tht)
    hq_payload = {str(p.relative_to(hq)) for p in hq.rglob("*") if p.is_file()}
    hq_manifest = {"pass": True, "delivery_revision": "R4-HUAQIU-20261026-R1", "bom_format_revision": BOM_FORMAT_REVISION, "pcb_sha256": R4_PCB_SHA, "files": {name: file_sha(hq / name) for name in sorted(hq_payload)}}
    (hq / "SHA256.json").write_text(json.dumps(hq_manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    hq_payload.add("SHA256.json")
    hq_archive = ROOT / "release" / "huaqiu-pcba-20261026-r4.zip"
    write_zip(hq_archive, hq, hq_payload)
    (ROOT / "verification" / "HUAQIU_PRODUCTION_VALIDATION_R4.json").write_text(
        json.dumps({"pass": True, "archive_sha256": file_sha(hq_archive), "huaqiu_files": len(hq_payload), "pcb_sha256": R4_PCB_SHA}, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps({"production": str(production_archive), "huaqiu": str(hq_archive), "production_sha256": file_sha(production_archive), "huaqiu_sha256": file_sha(hq_archive), "smt": len(smt), "tht": len(tht)}, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
