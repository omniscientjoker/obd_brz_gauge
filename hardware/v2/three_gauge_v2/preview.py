"""Render the exported STL geometry. Requires numpy and Pillow."""
import argparse
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
    return float(re.search(rf"^{name}\s*=\s*([\d.]+)", params, re.M)[1])


def mesh(name):
    data = (ROOT / "stl" / f"{name}.stl").read_text()
    vertices = re.findall(r"vertex\s+([-+\d.eE]+)\s+([-+\d.eE]+)\s+([-+\d.eE]+)", data)
    return np.array(vertices, dtype=float).reshape(-1, 3, 3)


image = Image.new("RGB", (1800, 1500), "#f4f6fa")
draw = ImageDraw.Draw(image)
font = ImageFont.truetype(args.font, 34)
small = ImageFont.truetype(args.font, 25)
title = ImageFont.truetype(args.font, 46)


def render(items, box):
    # Orthographic view from the rear and lower side of the assembly.
    up = np.array([0, 0.72, 0.694])
    eye = np.array([0, -0.694, 0.72])
    light = np.array([-0.3, -0.5, 0.81])
    projected_all = [np.stack([t[:, :, 0], t @ up], axis=-1) for t, _ in items]
    points = np.concatenate(projected_all).reshape(-1, 2)
    lo, hi = points.min(axis=0), points.max(axis=0)
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
    image.paste(Image.fromarray(pixels), box[:2])


draw.text((65, 38), "V2  可更换托架三联底座", font=title, fill="#1d2a38")
draw.text((68, 108),
          f"{value('case_w'):g} × {value('case_h'):g} × "
          f"{value('case_depth')+value('panel_t'):g} mm    |    3 × 1.85B    |    6 × M3 外壳固定",
          font=small, fill="#526171")
draw.text((65, 183), "面板 + 3 个独立托架（分解预览）", font=font, fill="#1d2a38")
items = [(mesh("panel"), [104, 121, 140])]
for i, name in enumerate(["carrier_left", "carrier_centre", "carrier_right"]):
    triangles = mesh(name)
    triangles[:, :, 1] *= -1
    triangles[:, :, 2] = value("carrier_h") - triangles[:, :, 2]
    triangles[:, :, 0] += (i-1)*value("module_pitch")
    triangles[:, :, 2] += value("panel_t")+value("panel_post_h")+14
    items.append((triangles, [230, 174, 67]))
render(items, (65, 250, 1735, 650))
draw.text((68, 700), "灰色：通用面板    金色：可替换托架    主板孔位仅配置在托架上",
          font=small, fill="#526171")
draw.line((65, 765, 1735, 765), fill="#d4dce5", width=2)
draw.text((65, 805), "后底盒（开口朝上）", font=font, fill="#1d2a38")
render([(mesh("base"), [111, 145, 169])], (65, 870, 1735, 1300))
draw.text((68, 1358), "6 个落底螺柱 / 内部走线槽 / 下侧 USB-C 开孔 / 背面磁环凹槽",
          font=small, fill="#526171")
draw.text((68, 1420), "孔位与轴向尺寸仍待实测：先打印校准片，再试装单个托架。",
          font=small, fill="#945d19")
image.save(ROOT / "preview.png")
