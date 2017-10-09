import subprocess
import atexit
import struct
import io
import numpy as np

# "Framebuffers" are always 256*240*4 rgb bytes.
# "NewTiles, NewSpriteTiles, TilesSoFar, SpriteTilesSoFar" are a length L,
# then L 268 byte sequences (int32 hash x int32 idx x int32 palette x (8*8*4) rgb).
# "TilesByPixel" is 256*240 units of int32 hash x Xscroll x Yscroll.
# "LiveSprites" is a count C followed by C 6-byte sequences
# (int32 hash x Xpos x Ypos).

# enum CtrlCommand {
CmdStep = 0  # , //Infos NumPlayers BytesPerPlayer NumMovesMSB NumMovesLSB MOVES
#           // -> steps emu. One FB, NewTiles, NewSpriteTiles per move; one TilesByPixel, LiveSprites at end of sequence
CmdGetState = 1  # , // -> state length, state
CmdLoadState = 2  # , //LoadState, infos, statelen, statebuf
#                // -> loads state; if infos & FB, sends framebuffer
CmdGetTilesSoFar = 3  # ,
CmdGetSpriteTilesSoFar = 4
# };

# enum InfoMask {
InfoNone = 0
InfoFB = 1 << 0
InfoNewTiles = 1 << 1
InfoNewSpriteTiles = 1 << 2
InfoTilesByPixel = 1 << 3
InfoLiveSprites = 1 << 4
InfoReservedA = 1 << 5
InfoReservedB = 1 << 6
InfoReservedC = 1 << 7
# };


def to_uint8(num):
    return struct.pack("B", num)


def to_uint16(num):
    return struct.pack("!H", num)


@atexit.register
def kill_subprocesses():
    remocon.kill()

remocon = subprocess.Popen(['Remocon/obj.x64/remocon', 'mario.nes'],
                           stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT,
                           bufsize=-1)

inp = io.open(remocon.stdin.fileno(), 'wb', closefd=False)
outp = io.open(remocon.stdout.fileno(), 'rb', closefd=False)

readbuf = memoryview(bytearray([0] * 1024 * 1024))
writebuf = memoryview(bytearray([0] * 1024 * 1024))


def wait_ready():
    assert outp.readinto(readbuf[:1]) == 1
    assert ord(readbuf[0]) == 0

# wait until READY 0 byte
wait_ready()
print("Success!")
# then send a step request with no info
writebuf[0] = chr(CmdStep)  # cmd
writebuf[1] = chr(InfoNone)  # infos
writebuf[2] = chr(1)  # 1 player
writebuf[3] = chr(1)  # 1 byte per player
moves = 60
writebuf[4:6] = to_uint16(moves)  # 60 moves
for i in range(0, moves):
    writebuf[6 + i] = chr(0)  # do nothing
inp.write(writebuf[:6 + moves])
inp.flush()
# then send a short step request with FB info
wait_ready()
writebuf[0] = chr(CmdStep)
writebuf[1] = chr(InfoFB)
writebuf[2] = chr(1)
writebuf[3] = chr(1)
writebuf[4:6] = to_uint16(4)
for i in range(4):
    writebuf[6 + i] = chr(0)
inp.write(writebuf[:6 + 4])
inp.flush()
# then write the framebuffers as PPM images
fblen = (256 * 240 * 4)
for i in range(4):
    assert outp.readinto(readbuf[:fblen]) == fblen
    header = bytearray(b"P6\n 256 240\n 255\n")
    ppmfile = open('testout/f' + str(i) + '.ppm', 'wb')
    ppmfile.write(header)
    for px in range(0, fblen, 4):
        ppmfile.write(bytearray([
            readbuf[px + 2], readbuf[px + 1], readbuf[px]
        ]))
    ppmfile.flush()
    ppmfile.close()
wait_ready()
print("All done!")
# then send a request for all tiles
# then draw those out as PPMs
