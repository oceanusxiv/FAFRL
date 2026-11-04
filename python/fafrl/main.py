from pathlib import Path

import cv2
import numpy as np

from fafrl.faf_repy import FafEnv


def main():
    env = FafEnv(
        "/run/media/eric/Gaming/.faforever/bin",
        Path("python/fafrl/faf_config.lua").read_text(),
    )
    env.reset()
    for _ in range(10000):
        if env.is_game_over():
            break
        env.step([])

if __name__ == "__main__":
    main()
