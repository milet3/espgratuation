from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


SRC_DIR = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\thesis_images")
OUT_FILE = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\thesis_images_contact_sheet.jpg")

THUMB_W = 260
THUMB_H = 180
PADDING = 20
LABEL_H = 28
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


def natural_key(path: Path):
    stem = path.stem
    digits = "".join(ch for ch in stem if ch.isdigit())
    return (stem.rstrip(digits), int(digits or 0), path.suffix.lower())


def main() -> None:
    files = sorted(
        [p for p in SRC_DIR.iterdir() if p.suffix.lower() in {".png", ".jpg", ".jpeg"}],
        key=natural_key,
    )
    rows = (len(files) + COLS - 1) // COLS
    width = PADDING + COLS * (THUMB_W + PADDING)
    height = PADDING + rows * (THUMB_H + LABEL_H + PADDING)
    sheet = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default()

    for idx, path in enumerate(files):
        row = idx // COLS
        col = idx % COLS
        x = PADDING + col * (THUMB_W + PADDING)
        y = PADDING + row * (THUMB_H + LABEL_H + PADDING)
        with Image.open(path) as img:
            thumb = fit_image(img)
        sheet.paste(thumb, (x, y))
        draw.rectangle((x, y, x + THUMB_W, y + THUMB_H), outline=(180, 180, 180), width=1)
        draw.text((x, y + THUMB_H + 6), path.name, fill=FG, font=font)

    OUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT_FILE, quality=90)
    print(OUT_FILE)


if __name__ == "__main__":
    main()
