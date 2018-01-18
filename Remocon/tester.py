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
    header = bytearray(b"P6\n {} {}\n 255\n".format(buf.shape[0],buf.shape[1]))
    ppmfile = open(fl, 'wb')
    ppmfile.write(header)
    
    for y in range(len(buf)):
        for x in range(len(buf[y])):
            ppmfile.write(bytearray([buf[y, x, 2], buf[y, x, 1], buf[y, x, 0]]))
    ppmfile.flush()
    ppmfile.close()



mario_controls = mesen.read_fm2('Illustrative.fm2')

mario_controls = map(mesen.moves_to_bytes,mario_controls)
print(len(mario_controls))

    
remo = Mesen("Remocon/obj.x64/remocon", "mario.nes")


startup = 500
results = remo.step([mario_controls[0][:startup]], Infos(framebuffer=False, live_sprites=False, tiles_by_pixel=False, new_tiles=False, new_sprite_tiles=False))
results = remo.step([mario_controls[0][startup:startup+1]], Infos(framebuffer=True, live_sprites=False, tiles_by_pixel=True, new_tiles=False, new_sprite_tiles=False))


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
        
        plt.imshow(pf.framebuffer[:,:,[2,1,0]],interpolation='none')
        plt.show()
    if pf.tiles_by_pixel is not None:
        tiles = np.zeros((len(pf.tiles_by_pixel),len(pf.tiles_by_pixel[0])))
        scrolls = np.zeros((len(pf.tiles_by_pixel),len(pf.tiles_by_pixel[0]),3))
        hashes = {}
        for row in range(len(pf.tiles_by_pixel)):
            for col in range(len(pf.tiles_by_pixel[0])):
                if pf.tiles_by_pixel[row][col].hash not in hashes:
                    hashes[pf.tiles_by_pixel[row][col].hash] = len(hashes)
                tiles[row,col] = hashes[pf.tiles_by_pixel[row][col].hash]
                scrolls[row,col,0] = pf.tiles_by_pixel[row][col].x_scroll/8.0
                scrolls[row,col,1] = pf.tiles_by_pixel[row][col].y_scroll/8.0
        plt.imshow(tiles,interpolation='none',cmap='viridis')
        plt.show()
        
        plt.imshow(scrolls,interpolation='none',cmap='viridis')
        plt.show()
'''
results = remo.step([[0] * 1], Infos(framebuffer=True, live_sprites=True, tiles_by_pixel=True, new_tiles=True, new_sprite_tiles=True))
i = 0


for pf in results[0]:
    i += 1
    fb = pf.framebuffer
    if fb is not None:
        dump_ppm(fb, "testout/f" + str(i) + ".ppm")
    if pf.tiles_by_pixel is not None:
        for row in range(len(pf.tiles_by_pixel)):
            for col in range(len(pf.tiles_by_pixel[0])):
                tile_here = pf.tiles_by_pixel[row][col]
                #print(tile_here.x_scroll)
    if pf.live_sprites is not None:
        print ("frame")
        for sprite in pf.live_sprites:
            print(sprite.hash, sprite.x, sprite.y)

# Test: new_tiles, new_sprite_tiles
if results[1].new_tiles is not None:
    # expect empty
    assert len(results[1].new_tiles) == 0
if results[1].new_sprite_tiles is not None:
    # expect empty
    assert len(results[1].new_sprite_tiles) == 0
'''
# # wait until READY 0 byte
# wait_ready()
# print("Success!")
# # then send a step request with no info
# writebuf[0] = chr(CmdStep)  # cmd
# writebuf[1] = chr(InfoNone)  # infos
# writebuf[2] = chr(1)  # 1 player
# writebuf[3] = chr(1)  # 1 byte per player
# moves = 60
# writebuf[4:6] = to_uint16(moves)  # 60 moves
# for i in range(0, moves):
#     writebuf[6 + i] = chr(0)  # do nothing
# inp.write(writebuf[:6 + moves])
# inp.flush()
# # then send a short step request with FB info
# wait_ready()
# writebuf[0] = chr(CmdStep)
# writebuf[1] = chr(InfoFB)
# writebuf[2] = chr(1)
# writebuf[3] = chr(1)
# writebuf[4:6] = to_uint16(4)
# for i in range(4):
#     writebuf[6 + i] = chr(0)
# inp.write(writebuf[:6 + 4])
# inp.flush()
# # then write the framebuffers as PPM images
# fblen = (256 * 240 * 4)
# for i in range(4):
#     assert outp.readinto(readbuf[:fblen]) == fblen
#     header = bytearray(b"P6\n 256 240\n 255\n")
#     ppmfile = open('testout/f' + str(i) + '.ppm', 'wb')
#     ppmfile.write(header)
#     for px in range(0, fblen, 4):
#         ppmfile.write(bytearray([
#             readbuf[px + 2], readbuf[px + 1], readbuf[px]
#         ]))
#     ppmfile.flush()
#     ppmfile.close()
# wait_ready()
# print("All done!")
# # then send a request for all tiles
# # then draw those out as PPMs
