# type check: MYPYPATH=../../mypy-data/numpy-mypy mypy --py2 tester.py
# run: python tester.py
from __future__ import print_function
import subprocess
import atexit
import struct
import io
import numpy as np
from typing import cast, Iterable, Dict, Tuple, List, Optional
from mesen import Mesen, Infos
import mesen


def dump_ppm(buf, fl):
    header = bytearray(b"P6\n {} {}\n 255\n".format(buf.shape[1], buf.shape[0]))
    ppmfile = open(fl, 'wb')
    ppmfile.write(header)

    for y in range(len(buf)):
        for x in range(len(buf[y])):
            ppmfile.write(bytearray([buf[y, x, 2], buf[y, x, 1], buf[y, x, 0]]))
    ppmfile.flush()
    ppmfile.close()


mario_controls = mesen.read_fm2('Illustrative.fm2')

mario_controls = map(mesen.moves_to_bytes, mario_controls)
#mario_controls = [[0, mesen.MoveStart, 0, 0, mesen.MoveA] * 300]
print(len(mario_controls[0]))

remo = Mesen("Remocon/obj.x64/remocon", "mario.nes")
#remo = Mesen("Remocon/obj.x64/remocon", "mario3.nes")

startup = 500
results = remo.step([mario_controls[0][:startup]], Infos(framebuffer=False, live_sprites=False, tiles_by_pixel=False, new_tiles=False, new_sprite_tiles=False))
results = remo.step([mario_controls[0][startup:startup + 1]], Infos(framebuffer=True, live_sprites=True, tiles_by_pixel=True, new_tiles=False, new_sprite_tiles=False))

#remo.step(mario_controls, Infos(framebuffer=False, live_sprites=True, tiles_by_pixel=True, new_tiles=True, new_sprite_tiles=True))
print("Steps done")
if results[1].new_tiles is not None:
    for t in results[1].new_tiles:
        dump_ppm(t.pixels, "testout/tt" + str(t.hash) + ".ppm")
if results[1].new_sprite_tiles is not None:
    for t in results[1].new_sprite_tiles:
        dump_ppm(t.pixels, "testout/st" + str(t.hash) + ".ppm")


import matplotlib.pyplot as plt
for pf in results[0]:
    if pf.framebuffer is not None:
        plt.imshow(pf.framebuffer[:, :, [2, 1, 0]], interpolation='none')
        plt.show()
    if pf.tiles_by_pixel is not None:
        tiles = np.zeros((len(pf.tiles_by_pixel), len(pf.tiles_by_pixel[0])))
        scrolls = np.zeros((len(pf.tiles_by_pixel), len(pf.tiles_by_pixel[0]), 3))
        hashes = {}
        for row in range(len(pf.tiles_by_pixel)):
            for col in range(len(pf.tiles_by_pixel[0])):
                if pf.tiles_by_pixel[row][col].hash not in hashes:
                    hashes[pf.tiles_by_pixel[row][col].hash] = len(hashes)
                tiles[row, col] = hashes[pf.tiles_by_pixel[row][col].hash]
                scrolls[row, col, 0] = pf.tiles_by_pixel[row][col].x_scroll / 8.0
                scrolls[row, col, 1] = pf.tiles_by_pixel[row][col].y_scroll / 8.0
        plt.imshow(tiles, interpolation='none', cmap='viridis')
        plt.show()
        plt.imshow(scrolls, interpolation='none', cmap='viridis')
        plt.show()
