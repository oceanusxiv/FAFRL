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
    obs = env.step()
    print(obs.heightmap.shape)
    uint8_map = cv2.normalize(obs.heightmap, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)
    terrain_bgr = cv2.applyColorMap(uint8_map, cv2.COLORMAP_BONE)
    while True:
        cv2.imshow("RTS Global Tactical Viewport", terrain_bgr)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

if __name__ == "__main__":
    main()
