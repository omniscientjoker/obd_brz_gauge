"""Render the exported STL geometry. Requires numpy and Pillow."""
import argparse
import json
from pathlib import Path
import re

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--font", default="/System/Library/Fonts/STHeiti Light.ttc")
args = parser.parse_args()
params = (ROOT / "params.scad").read_text()


def value(name):
    return float(re.search(rf"^{name}\s*=\s*([-+\d.]+)", params, re.M)[1])


def mesh(name):
    data = (ROOT / "stl" / f"{name}.stl").read_text()
    vertices = re.findall(r"vertex\s+([-+\d.eE]+)\s+([-+\d.eE]+)\s+([-+\d.eE]+)", data)
    return np.array(vertices, dtype=float).reshape(-1, 3, 3)


image = Image.new("RGB", (1800, 1500), "#f4f6fa")
draw = ImageDraw.Draw(image)
font = ImageFont.truetype(args.font, 34)
small = ImageFont.truetype(args.font, 25)
title = ImageFont.truetype(args.font, 46)


def render(items, box, canvas=image, rear_flat=False, view=None, bounds=None):
    # Orthographic view from the rear and lower side of the assembly.
    up = np.array([0, 1, 0] if rear_flat else [0, 0.72, 0.694])
    eye = np.array([0, 0, 1] if rear_flat else [0, -0.694, 0.72])
    if view == "bottom":
        up, eye = np.array([0, 1, 0]), np.array([0, 0, -1])
    elif view == "usb":
        up, eye = np.array([0, 0, 1]), np.array([0, 1, 0])
    light = np.array([-0.3, -0.5, 0.81])
    projected_all = [np.stack([t[:, :, 0], t @ up], axis=-1) for t, _ in items]
    points = np.concatenate(projected_all).reshape(-1, 2)
    lo, hi = points.min(axis=0), points.max(axis=0)
    if bounds is not None:
        lo, hi = np.array(bounds, dtype=float)
    width, height = box[2]-box[0], box[3]-box[1]
    scale = min((width-40)/(hi[0]-lo[0]), (height-20)/(hi[1]-lo[1]))
    origin = (lo+hi)/2
    pixels = np.full((height, width, 3), [244, 246, 250], dtype=np.uint8)
    zbuffer = np.full((height, width), -np.inf)
    for triangles, colour in items:
        normals = np.cross(triangles[:, 1]-triangles[:, 0],
                           triangles[:, 2]-triangles[:, 0])
        lengths = np.linalg.norm(normals, axis=1)
        normals /= np.maximum(lengths[:, None], 1e-12)
        lighting = 0.50 + 0.50*np.clip(normals @ light, 0, 1)
        projected = np.stack([triangles[:, :, 0], triangles @ up], axis=-1)
        projected[:, :, 0] = width/2 + scale*(projected[:, :, 0]-origin[0])
        projected[:, :, 1] = height/2 - scale*(projected[:, :, 1]-origin[1])
        depths = triangles @ eye
        for i in range(len(triangles)):
            if normals[i] @ eye <= 1e-8:
                continue
            p = projected[i]
            xmin, ymin = np.maximum(np.floor(p.min(axis=0)).astype(int), 0)
            xmax, ymax = np.minimum(np.ceil(p.max(axis=0)).astype(int),
                                   [width-1, height-1])
            if xmin > xmax or ymin > ymax:
                continue
            denominator = (p[1, 1]-p[2, 1])*(p[0, 0]-p[2, 0]) + \
                (p[2, 0]-p[1, 0])*(p[0, 1]-p[2, 1])
            if abs(denominator) < 1e-9:
                continue
            yy, xx = np.mgrid[ymin:ymax+1, xmin:xmax+1] + 0.5
            a = ((p[1, 1]-p[2, 1])*(xx-p[2, 0]) +
                 (p[2, 0]-p[1, 0])*(yy-p[2, 1]))/denominator
            b = ((p[2, 1]-p[0, 1])*(xx-p[2, 0]) +
                 (p[0, 0]-p[2, 0])*(yy-p[2, 1]))/denominator
            c = 1-a-b
            depth = a*depths[i, 0] + b*depths[i, 1] + c*depths[i, 2]
            region = zbuffer[ymin:ymax+1, xmin:xmax+1]
            visible = (a >= -1e-8) & (b >= -1e-8) & (c >= -1e-8) & (depth > region)
            region[visible] = depth[visible]
            pixels[ymin:ymax+1, xmin:xmax+1][visible] = \
                (np.array(colour)*lighting[i]).astype(np.uint8)
    canvas.paste(Image.fromarray(pixels), box[:2])
    return lambda x, y: (box[0]+width/2+scale*(x-origin[0]),
                         box[1]+height/2-scale*(y-origin[1]))


draw.text((65, 38), "V2  可更换托架三联底座", font=title, fill="#1d2a38")
draw.text((68, 108),
          f"{value('case_w'):g} × {value('case_h'):g} × "
          f"{value('case_depth')+value('panel_t'):g} mm    |    3 × 1.85B    |    6 × M3 外壳固定",
          font=small, fill="#526171")
draw.text((65, 183), "通用面板 + A / B / A 开口托架（分解预览）", font=font, fill="#1d2a38")
items = [(mesh("panel"), [104, 121, 140])]
for i, name in enumerate(["carrier_a", "carrier_b", "carrier_a"]):
    triangles = mesh(name)
    triangles[:, :, 1] *= -1
    triangles[:, :, 2] = value("carrier_h") - triangles[:, :, 2]
    triangles[:, :, 0] += (i-1)*value("module_pitch")
    triangles[:, :, 2] += value("panel_t")+value("panel_post_h")+14
    items.append((triangles, [230, 174, 67]))
render(items, (65, 250, 1735, 650))
draw.text((68, 700), "USB-C朝6点钟 / A、B均有14 mm开口 / 9、12、3点固定 / 先插线再合底盒",
          font=small, fill="#526171")
draw.line((65, 765, 1735, 765), fill="#d4dce5", width=2)
draw.text((65, 805), "后底盒 + 中央 R3 供电板校准件（开口朝上）", font=font, fill="#1d2a38")
base_items = [(mesh("base"), [111, 145, 169])]
centre = json.loads(re.search(r"^power_pcb_centre\s*=\s*(\[[^;]+);", params, re.M)[1])
for i, name in enumerate(["power_board_mockup"]):
    triangles = mesh(name)
    triangles[:, :, 1] *= -1
    triangles[:, :, 0] += centre[0]
    triangles[:, :, 1] -= centre[1]
    triangles[:, :, 2] += value("bottom_t")+value("aux_post_h")
    base_items.append((triangles, [57, 147, 109] if i == 0 else [128, 94, 172]))
render(base_items, (65, 870, 1735, 1300))
draw.text((68, 1358), f"中央：{value('power_pcb_w'):g}×{value('power_pcb_h'):g}×{value('power_pcb_t'):g} mm / 孔距74 mm / 2个M2柱；USB中央凸舌。",
          font=small, fill="#526171")
draw.text((68, 1420), "底部12个3×8 mm通风槽避开PCB和磁环；插头、线束弯曲及器件高度需实物试装。",
          font=small, fill="#945d19")
image.save(ROOT / "preview.png")

# Read the production STLs and restore the rear-view assembly orientation.
# The print files are flipped so that their PCB contact pads sit on the bed.
total_depth = value("case_depth")+value("panel_t")
detail = Image.new("RGB", (1800, 1060), "#f4f6fa")
dd = ImageDraw.Draw(detail)
dd.text((65, 38), "A / B 托架：固定耳与USB-C开口", font=title, fill="#1d2a38")
dd.text((68, 110), "后视装配方向（非STL贴床视角）；9、12、3点固定，USB-C朝6点钟。",
        font=small, fill="#526171")
for i, name in enumerate(["carrier_a", "carrier_b"]):
    triangles = mesh(name)
    triangles[:, :, 1] *= -1
    triangles[:, :, 2] = value("carrier_h")-triangles[:, :, 2]
    left = 100+900*i
    project = render([(triangles, [230, 174, 67])],
                     (left, 240, left+700, 860), canvas=detail, rear_flat=True)
    dd.text((left+350, 195), name+".stl", font=font, fill="#1d2a38", anchor="mm")
    for x, y, label in [(-32.5,0,"9点"), (0,32.5,"12点"), (32.5,0,"3点")]:
        px, py = project(x, y)
        dd.text((px+(0 if x == 0 else (-58 if x < 0 else 58)),
                 py-55 if x == 0 else py), label, font=small,
                fill="#1d2a38", anchor="mm")
    px, py = project(0, -31)
    dd.text((px, py+32), "6点 USB-C / 14 mm贯穿", font=small,
            fill="#236e96", anchor="mm")
dd.text((68, 940), "面板对应柱同步移动；主板三孔、支撑高度和铜螺母规格保持原值。",
        font=small, fill="#526171")
dd.text((68, 990), f"231 × 82 × {total_depth:g} mm / 中心距75 mm / 面板、底盒及托架须使用本版配套打印件。",
        font=small, fill="#945d19")
detail.save(ROOT / "carrier_detail.png")

# Dimensioned rear-view placement; real component footprints are not selected.
placement = Image.new("RGB", (1800, 1120), "#f4f6fa")
pd = ImageDraw.Draw(placement)
origin = np.array([900, 530])
scale = min(7.5, 1660/value("case_w"))


def xy(x, y):
    return tuple(origin+np.array([x, -y])*scale)


def rect(x, y, w, h):
    return (*xy(x-w/2, y+h/2), *xy(x+w/2, y-h/2))


def centred_text(x, y, text, face=small, fill="#1d2a38"):
    pd.text(xy(x, y), text, font=face, fill=fill, anchor="mm")


pd.text((65, 42), "中央 R3 供电板安装（整机背视坐标）", font=title, fill="#1d2a38")
power_gap = (total_depth-value("bottom_t")-value("aux_post_h")-value("power_pcb_t")
             -value("power_components_h")-value("panel_t")-value("panel_post_h")
             -value("carrier_h")-value("pcb_t")-value("pcb_rear_component_h"))
pd.text((68, 112), f"供电板{value('power_pcb_w'):g}×{value('power_pcb_h'):g}×{value('power_pcb_t'):g} mm / 孔心中点XY=({centre[0]:g},{centre[1]:g}) / 孔距74 mm；总厚{total_depth:g} mm",
        font=small, fill="#526171")
pd.rounded_rectangle(rect(0, 0, value("case_w"), value("case_h")),
                     radius=value("case_h")/2*scale, fill="#e2e9ef",
                     outline="#536a7c", width=4)
pd.ellipse(rect(0, 0, value("magnet_pocket_d"), value("magnet_pocket_d")),
           outline="#9ca8b1", width=3)
path_points = json.loads(re.search(r"^cable_path_points\s*=\s*(\[[^;]+);", params, re.M)[1])
vent_x = json.loads(re.search(r"^vent_centres_x\s*=\s*(\[[^;]+);", params, re.M)[1])
vent_y = json.loads(re.search(r"^vent_centres_y\s*=\s*(\[[^;]+);", params, re.M)[1])
for x in vent_x:
    for y in vent_y:
        pd.rounded_rectangle(rect(x, y, value('vent_slot_w'), value('vent_slot_h')),
                             radius=value('vent_slot_r')*scale,
                             fill="#f4f6fa", outline="#536a7c", width=2)
path_pixels = [xy(*p) for p in path_points]
for i in (0,2):
    pd.line(path_pixels[i:i+2], fill="#9fb0bc", width=round((value("cable_channel_w")+4)*scale))
    pd.line(path_pixels[i:i+2], fill="#f4f6fa", width=round(value("cable_channel_w")*scale))
# Exterior cable plugs directly into J1 through the continuous wall.
usb_path = [(0,-47),(0,value('power_usb_face_y'))]
pd.line([xy(*p) for p in usb_path], fill="#2a87b5", width=6, joint="curve")
for i, c in enumerate([centre]):
    fill = "#c0e2d1" if i == 0 else "#d9cde8"
    w = value("power_pcb_w")
    h = value("power_pcb_h")
    outline = json.loads(re.search(r"^power_pcb_outline_xy\s*=\s*(\[[^;]+);", params, re.M)[1])
    pd.polygon([xy(c[0]+x,c[1]+y) for x,y in outline], fill=fill,
               outline="#536a7c", width=3)
    holes = [(-value('power_hole_pitch')/2,0),(value('power_hole_pitch')/2,0)]
    for hx, hy in holes:
        pd.ellipse(rect(c[0]+hx,c[1]+hy,value("aux_hole_d"),value("aux_hole_d")),
                   fill="#f4f6fa", outline="#536a7c", width=2)
    centred_text(c[0], 3.2, "R3 三路固定5V供电PCB")
    centred_text(c[0], -3.2, f"X={c[0]:g}  Y={c[1]:g}")
    centred_text(c[0], h/2+9.5, f"2×M2，柱高{value('aux_post_h'):g} mm")
for x in [-21.9,0.5,22.9]:
    pd.rectangle(rect(x,(value('power_xh_front_y')+value('power_xh_envelope_ymin'))/2,
                      8,value('power_xh_front_y')-value('power_xh_envelope_ymin')),
                 fill='#dea84a',outline='#536a7c',width=2)
centred_text(0,25,"XH输出朝上 / 先插线再合盒")
centred_text(0,-23,"USB-C PD输入朝下")
pd.rectangle(rect(0,value('power_usb_face_y')+2.0,value('power_usb_shell_w'),4.0),
             fill='#2a87b5')
centred_text(0, -50, "GCT USB4085直插 / 连续2.4 mm壁 / 名义露出0.2 mm")
pd.text((68, 945), f"元件高度目标{value('power_components_h'):g} mm；距仪表后部包络{power_gap:.1f} mm；底板3 mm。", font=small, fill="#526171")
pd.text((68, 990), "USB-C圆角孔9.74×3.96 mm；取消凹位与薄挡片；底部12个3×8 mm散热槽。", font=small, fill="#526171")
pd.text((68, 1035), "磁吸圆环凹槽保留；完整12×8 mm插头胶壳包络参与检查；实际插头需试装。", font=small, fill="#945d19")
placement.save(ROOT / "electronics" / "placement.png")

# Exterior views use the exported production base STL. The metal socket is
# only a visual envelope; it is not an additional printable part.
base_detail = Image.new("RGB", (1800, 1220), "#f4f6fa")
bd = ImageDraw.Draw(base_detail)
bd.text((65, 38), "底盒：USB-C护口与散热隔栅", font=title, fill="#1d2a38")
bd.text((68, 115), "外底面 / 12个3×8 mm贯穿槽 / 中央磁环凹槽保留", font=small, fill="#526171")
base_mesh = mesh('base')
pocket_floor = np.all(np.abs(base_mesh[:, :, 2]-value('magnet_pocket_depth')) < 1e-4, axis=1)
render([(base_mesh[~pocket_floor], [111, 145, 169]),
        (base_mesh[pocket_floor], [151, 177, 195])], (65, 170, 1735, 755),
       canvas=base_detail, view="bottom")
bd.line((65, 775, 1735, 775), fill="#d4dce5", width=2)
bd.text((65, 800), "USB-C侧壁局部放大（含母座包络）", font=font, fill="#1d2a38")


def box_mesh(low, high):
    corners = np.array([[x, y, z] for x in (low[0], high[0])
                        for y in (low[1], high[1]) for z in (low[2], high[2])])
    indices = np.array([[0, 1, 3], [0, 3, 2], [4, 6, 7], [4, 7, 5],
                        [0, 4, 5], [0, 5, 1], [2, 3, 7], [2, 7, 6],
                        [0, 2, 6], [0, 6, 4], [1, 5, 7], [1, 7, 3]])
    return corners[indices]


axis_from_bottom = (value('bottom_t')+value('aux_post_h')+value('power_pcb_t')
                    +value('power_usb_axis_above_pcb'))
socket_face = -value('power_usb_face_y')
sw, sh = value('power_usb_shell_w'), value('power_usb_mouth_h')
metal_t = 0.3
usb_items = [(base_mesh, [111, 145, 169])]


def mouth_contour(w, h, radius):
    points = []
    for cx, cz, start in [(w/2-radius, h/2-radius, 0),
                          (-w/2+radius, h/2-radius, 90),
                          (-w/2+radius, -h/2+radius, 180),
                          (w/2-radius, -h/2+radius, 270)]:
        for a in np.linspace(start, start+90, 13):
            points.append([cx+radius*np.cos(np.radians(a)),
                           cz+radius*np.sin(np.radians(a))])
    return np.array(points)


outer = mouth_contour(sw, sh, value('power_usb_mouth_r'))
inner = mouth_contour(sw-2*metal_t, sh-2*metal_t, value('power_usb_mouth_r')-metal_t)
metal, dark = [], []
for i in range(len(outer)):
    j = (i+1) % len(outer)
    a, b, c, d = [np.array([p[0], socket_face, axis_from_bottom+p[1]])
                  for p in (outer[i], outer[j], inner[j], inner[i])]
    metal.extend([[a, c, b], [a, d, c]])
    dark.append([[0, socket_face-0.01, axis_from_bottom],
                 [inner[j, 0], socket_face-0.01, axis_from_bottom+inner[j, 1]],
                 [inner[i, 0], socket_face-0.01, axis_from_bottom+inner[i, 1]]])
usb_items.extend([(np.array(metal), [193, 207, 218]), (np.array(dark), [33, 40, 48])])
render(usb_items, (65, 860, 940, 1170), canvas=base_detail, view="usb",
       bounds=((-15, 1), (15, 17)))
for i, line in enumerate(["连续一体侧壁：完整2.4 mm厚",
                           "圆角孔：9.74 × 3.96 mm / R1.8",
                           "GCT母座名义露出0.2 mm",
                           "取消凹位、加强框和薄挡片",
                           "孔中心距外底面8.57 mm；需实物试装"]):
    bd.text((985, 900+i*52), line, font=small, fill="#526171")
base_detail.save(ROOT / "base_detail.png")
