"""生成 windows/Assets/AppIcon.ico（多尺寸，供 exe 资源与快捷方式使用）。

用法：python tools/make_icon.py
"""

import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(HERE, "..", "Assets", "AppIcon.ico")

SIZE = 1024
RADIUS = int(SIZE * 0.22)
TOP = (36, 126, 230)
BOTTOM = (9, 72, 158)


def make_background() -> Image.Image:
    gradient = Image.new("RGBA", (SIZE, SIZE))
    pixels = gradient.load()
    for y in range(SIZE):
        t = y / (SIZE - 1)
        color = tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3))
        for x in range(SIZE):
            pixels[x, y] = (*color, 255)

    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=RADIUS, fill=255)

    base = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    base.paste(gradient, (0, 0), mask)
    return base


def draw_glyph(image: Image.Image) -> None:
    draw = ImageDraw.Draw(image)
    white = (255, 255, 255, 255)
    stroke = int(SIZE * 0.062)
    half = stroke // 2

    body = (int(SIZE * 0.19), int(SIZE * 0.24), int(SIZE * 0.81), int(SIZE * 0.63))
    draw.rounded_rectangle(body, radius=int(SIZE * 0.07), outline=white, width=stroke)

    center = SIZE // 2
    draw.line([(center, int(SIZE * 0.63)), (center, int(SIZE * 0.745))], fill=white, width=stroke)
    draw.line([(int(SIZE * 0.33), int(SIZE * 0.78)), (int(SIZE * 0.67), int(SIZE * 0.78))], fill=white, width=stroke)

    # 圆头
    for x, y in (
        (center, int(SIZE * 0.745)),
        (int(SIZE * 0.33), int(SIZE * 0.78)),
        (int(SIZE * 0.67), int(SIZE * 0.78)),
    ):
        draw.ellipse([x - half, y - half, x + half, y + half], fill=white)


def main() -> None:
    image = make_background()
    draw_glyph(image)
    image = image.resize((256, 256), Image.LANCZOS)
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    image.save(
        OUTPUT,
        format="ICO",
        sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    print("wrote", os.path.normpath(OUTPUT))


if __name__ == "__main__":
    main()
