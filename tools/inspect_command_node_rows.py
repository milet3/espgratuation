from pathlib import Path

import numpy as np
from PIL import Image


IMG = Path(r"C:\Users\Milet\Desktop\ESP32gratuation - 2\espgratuation - 2\artifacts\image9_command_region.png")


def main() -> None:
    arr = np.array(Image.open(IMG).convert("L"))
    for y in range(60, 250, 5):
        xs = np.where(arr[y] < 180)[0]
        if len(xs):
            print(y, int(xs.min()), int(xs.max()), int(len(xs)))


if __name__ == "__main__":
    main()
