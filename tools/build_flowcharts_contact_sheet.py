from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


SRC_DIR = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\thesis_images")
OUT_FILE = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\thesis_flowcharts_contact_sheet.jpg")

FLOWCHARTS = [
    ("图2.1", "image2.png"),
    ("图2.2", "image3.png"),
    ("图2.3", "image4.png"),
    ("图2.4", "image5.png"),
    ("图4.1", "image6.png"),
    ("图4.2", "image7.png"),
    ("图4.3", "image8.png"),
    ("图4.4", "image9.png"),
    ("图4.5", "image10.png"),
    ("图4.6", "image11.png"),
    ("图4.7", "image12.png"),
    ("图4.8", "image13.png"),
]

THUMB_W = 300
THUMB_H = 220
PADDING = 20
LABEL_H = 42
COLS = 3
BG = (245, 245, 245)
FG = (20, 20, 20)


def fit_image(img: Image.Image) -> Image.Image:
    canvas = Image.new("RGB", (THUMB_W, THUMB_H), "white")
    src = img.convert("RGB")
    src.thumbnail((THUMB_W, THUMB_H))
    x = (THUMB_W - src.width) // 2
    y = (THUMB_H - src.height) // 2
    canvas.paste(src, (x, y))
    return canvas


def main() -> None:
    rows = (len(FLOWCHARTS) + COLS - 1) // COLS
    width = PADDING + COLS * (THUMB_W + PADDING)
    height = PADDING + rows * (THUMB_H + LABEL_H + PADDING)
    sheet = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()

    for idx, (label, filename) in enumerate(FLOWCHARTS):
        path = SRC_DIR / filename
        row = idx // COLS
        col = idx % COLS
        x = PADDING + col * (THUMB_W + PADDING)
        y = PADDING + row * (THUMB_H + LABEL_H + PADDING)
        with Image.open(path) as img:
            thumb = fit_image(img)
        sheet.paste(thumb, (x, y))
        draw.rectangle((x, y, x + THUMB_W, y + THUMB_H), outline=(180, 180, 180), width=1)
        draw.text((x, y + THUMB_H + 6), f"{label}  {filename}", fill=FG, font=font)

    OUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT_FILE, quality=92)
    print(OUT_FILE)


if __name__ == "__main__":
    main()
