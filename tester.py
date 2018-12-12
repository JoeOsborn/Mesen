# type check: MYPYPATH=../../mypy-data/numpy-mypy mypy --py2 tester.py
# run: python tester.py
import matplotlib.pyplot as plt
import subprocess
import atexit
import struct
import io
import numpy as np
from typing import cast, Iterable, Dict, Tuple, List, Optional
from mesen import Mesen, Infos
import mesen


def dump_ppm(buf, fl):
    header = bytearray("P6\n {} {}\n 255\n".format(buf.shape[1], buf.shape[0]), "utf-8")
    ppmfile = open(fl, 'wb')
    ppmfile.write(header)

    for y in range(len(buf)):
        for x in range(len(buf[y])):
            ppmfile.write(bytearray([buf[y, x, 2], buf[y, x, 1], buf[y, x, 0]]))
    ppmfile.flush()
    ppmfile.close()


mario_controls = mesen.read_fm2('Illustrative.fm2')
mario_controls = list(map(mesen.moves_to_bytes, mario_controls))
#mario_controls = [[0, mesen.MoveStart, 0, 0, mesen.MoveA] * 300]
print(len(mario_controls[0]))

remo = Mesen("Remocon/obj.x64/remocon", "mario.nes")
#remo = Mesen("Remocon/obj.x64/remocon", "mario3.nes")

startup = 650
results_0 = remo.step([mario_controls[0][:startup]], Infos(framebuffer=False, live_sprites=False, tiles_by_pixel=False, new_tiles=True, new_sprite_tiles=True))
results = remo.step([mario_controls[0][startup:startup + 1]], Infos(framebuffer=True, live_sprites=True, tiles_by_pixel=True, new_tiles=False, new_sprite_tiles=False))

#remo.step(mario_controls, Infos(framebuffer=False, live_sprites=True, tiles_by_pixel=True, new_tiles=True, new_sprite_tiles=True))
print("Steps done")
if results_0[1].new_tiles is not None:
    print("Dump tiles:", len(results_0[1].new_tiles))
    for t in results_0[1].new_tiles:
        dump_ppm(t.pixels, "testout/tt" + str(t.hash) + ".ppm")
    print("Done")
if results_0[1].new_sprite_tiles is not None:
    print("Dump sprite tiles:", len(results_0[1].new_sprite_tiles))
    for t in results_0[1].new_sprite_tiles:
        dump_ppm(t.pixels, "testout/st" + str(t.hash) + ".ppm")
    print("Done")

t = 0
for pf in results[0]:
    print(t)
    t += 1
    if pf.live_sprites is not None:
        print("Live sprites:", len(pf.live_sprites))
        for s in pf.live_sprites:
            print(s.hash, s.x, s.y)
    if pf.framebuffer is not None:
        print("FB")
        plt.imshow(pf.framebuffer[:, :, [2, 1, 0]], interpolation='none')
        plt.show()
    if pf.tiles_by_pixel is not None:
        w = len(pf.tiles_by_pixel[0])
        h = len(pf.tiles_by_pixel)
        print("tiles by pixel and scroll:", w, h)
        tiles = np.zeros((h, w))
        scrolls = np.zeros((h, w, 3))
        hashes: Dict[int, int] = {}
        for row in range(h):
            for col in range(w):
                if pf.tiles_by_pixel[row][col].hash not in hashes:
                    hashes[pf.tiles_by_pixel[row][col].hash] = len(hashes)
                tiles[row, col] = hashes[pf.tiles_by_pixel[row][col].hash]
                scrolls[row, col, 0] = pf.tiles_by_pixel[row][col].x_scroll / 8.0
                scrolls[row, col, 1] = pf.tiles_by_pixel[row][col].y_scroll / 8.0
        print("BG tiles")
        plt.imshow(tiles, interpolation='none', cmap='viridis')
        plt.show()
        print("Scroll info")
        plt.imshow(scrolls, interpolation='none', cmap='viridis')
        plt.show()
