#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""根据颜色去除地图图片背景,仅保留接近黑色的像素并转为纯黑。

原理:
  地图为灰度线条图。以像素亮度(luminance)为判据:
    - 原本可见(alpha > min_alpha)且亮度 <= black_threshold:视为黑色线条,
      直接变为 *纯黑*(RGB 0,0,0)且不透明(alpha 255)
    - 其余像素(浅色或原本透明):直接舍弃(alpha 0)
  注意:原本透明的像素(可能残留任意 RGB)不会因亮度低而被误判为线条。

用法:
  python3 去背景.py [输入.png] [输出.png]
  python3 去背景.py                       # 默认 图片/地图.png -> 图片/地图_去背景.png
"""
from PIL import Image
import numpy as np
import sys


def remove_background(src, dst, black_threshold=120, min_alpha=40):
    im = Image.open(src).convert("RGBA")
    a = np.array(im).astype(np.float32)

    # 灰度图:亮度取 RGB 均值 (0~255)
    lum = a[..., :3].mean(axis=-1)
    visible = a[..., 3] > min_alpha                 # 原本可见的像素
    keep = visible & (lum <= black_threshold)       # 接近黑色 -> 纯黑

    a[keep, :3] = 0      # 变为纯黑
    a[keep, 3] = 255     # 不透明
    a[~keep, 3] = 0      # 其余直接舍弃

    Image.fromarray(a.astype(np.uint8), "RGBA").save(dst)
    print(f"完成:{dst} 尺寸 {im.size} (black_threshold={black_threshold}, min_alpha={min_alpha})")


if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "图片/地图.png"
    dst = sys.argv[2] if len(sys.argv) > 2 else "图片/地图_去背景.png"
    remove_background(src, dst)
