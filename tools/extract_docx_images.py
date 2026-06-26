import shutil
import zipfile
from pathlib import Path


DOCX_PATH = Path(r"C:\Users\Milet\Desktop\毕设资料\论文.docx")
OUT_DIR = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\thesis_images")


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(DOCX_PATH) as zf:
        media_files = sorted(
            name for name in zf.namelist() if name.startswith("word/media/")
        )
        for name in media_files:
            target = OUT_DIR / Path(name).name
            with zf.open(name) as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst)
            print(target)


if __name__ == "__main__":
    main()
