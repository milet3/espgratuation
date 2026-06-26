from pathlib import Path

from PIL import Image


SRC = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\thesis_images\image9.png")
OUT = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\image9_command_region.png")


def main() -> None:
    img = Image.open(SRC)
    crop = img.crop((500, 500, 900, 800))
    crop.save(OUT)
    print(OUT)


if __name__ == "__main__":
    main()
