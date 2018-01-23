from __future__ import print_function
import sys
import subprocess
import atexit
import struct
import io
import numpy as np
from typing import cast, Iterable, Dict, Tuple, List, Optional
from collections import namedtuple

# "Framebuffers" are always 256*240*4 rgb bytes.
# "NewTiles, NewSpriteTiles, TilesSoFar, SpriteTilesSoFar" are a length L,
# then L 268 byte sequences (int32 hash x int32 idx x int32 palette x (8*8*4) rgb).
# "TilesByPixel" is 256*240 units of int32 hash x Xscroll x Yscroll.
# "LiveSprites" is a count C followed by C 6-byte sequences
# (int32 hash x Xpos x Ypos).

# enum CtrlCommand {
CmdStep = 0  # , //Infos NumPlayers BytesPerPlayer NumMovesMSB NumMovesLSB MOVES
#           // -> steps emu. One FB, TilesByPixel, LiveSprites per move; one NewTiles, NewSpriteTiles per sequence
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


def from_uint32(byte_slice):
    return struct.unpack("@I", byte_slice)[0]


def to_uint8(num):
    return struct.pack("@B", num)


def to_uint16(num):
    return struct.pack("@H", num)


Tile = namedtuple("Tile", ["hash", "index", "palette", "pixels"])
Summary = namedtuple("Summary", ["new_tiles", "new_sprite_tiles"])
PixelTileData = namedtuple("PixelTileData", ["hash", "x_scroll", "y_scroll"])
Sprite = namedtuple("Sprite", ["hash", "horizontal_mirroring", "vertical_mirroring", "background_priority", "x", "y"])
PerFrame = namedtuple("PerFrame", ["framebuffer", "tiles_by_pixel", "live_sprites"])
Infos = namedtuple("Infos", ["framebuffer", "new_tiles", "new_sprite_tiles", "tiles_by_pixel", "live_sprites"])
Infos.__new__.__defaults__ = (None,) * len(Infos._fields)


def infos_to_byte(infos):
    # type: (Infos) -> int
    mask = InfoNone
    if infos.framebuffer:
        mask |= InfoFB
    if infos.new_tiles:
        mask |= InfoNewTiles
    if infos.new_sprite_tiles:
        mask |= InfoNewSpriteTiles
    if infos.tiles_by_pixel:
        mask |= InfoTilesByPixel
    if infos.live_sprites:
        mask |= InfoLiveSprites
    return mask


class Mesen(object):
    __slots__ = ("mesen", "rom", "process", "inp", "outp", "readbuf", "writebuf",
                 "num_players", "bytes_per_player", "framebuffer_length",
                 "framebuffer_height", "framebuffer_width", "framebuffer_depth")
    # TODO: type the above

    def __init__(self, mesen, romfile, num_players=1, bytes_per_player=1, framebuffer_height=256, framebuffer_width=240, framebuffer_depth=4):
        # type: (str, str, int, int, int, int, int) -> None
        self.mesen = mesen
        self.rom = romfile
        self.num_players = num_players
        assert self.num_players > 0
        self.bytes_per_player = bytes_per_player
        assert self.bytes_per_player == 1
        self.framebuffer_height = framebuffer_height
        self.framebuffer_width = framebuffer_width
        self.framebuffer_depth = framebuffer_depth
        self.framebuffer_length = self.framebuffer_height * self.framebuffer_width * self.framebuffer_depth
        assert self.framebuffer_height == 256
        assert self.framebuffer_width == 240
        assert self.framebuffer_depth == 4
        self.process = subprocess.Popen([self.mesen, self.rom],
                                        stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE,
                                        stderr=sys.stdout,
                                        bufsize=-1)
        self.inp = cast(io.BufferedWriter, io.open(self.process.stdin.fileno(), 'wb', closefd=False))
        self.outp = cast(io.BufferedReader, io.open(self.process.stdout.fileno(), 'rb', closefd=False))
        self.readbuf = memoryview(bytearray([0] * 1024 * 1024))
        self.writebuf = memoryview(bytearray([0] * 1024 * 1024))
        self.wait_ready()

    def __del__(self):
        # type: () -> None
        self.process.kill()

    def wait_ready(self):
        # type: () -> None
        assert self.outp.readinto(cast(bytearray, self.readbuf[:1])) == 1
        assert ord(self.readbuf[0]) == 0

    def read_tile_sequence(self):
        # type: () -> List[Tile]
        new_tiles = []
        assert self.outp.readinto(cast(bytearray, self.readbuf[:4])) == 4
        how_many = from_uint32(self.readbuf[0:4])
        print("hm", how_many)
        assert self.outp.readinto(cast(bytearray, self.readbuf[:(how_many * (4 + 4 + 4 + 8 * 8 * 4))])) == (how_many * (4 + 4 + 4 + 8 * 8 * 4))
        read_idx = 0
        for tile_idx_ in range(how_many):
            tile_hash = from_uint32(self.readbuf[read_idx:read_idx + 4])
            read_idx += 4
            tile_idx = from_uint32(self.readbuf[read_idx:read_idx + 4])
            read_idx += 4
            tile_pal = from_uint32(self.readbuf[read_idx:read_idx + 4])
            read_idx += 4

            other = self.readbuf[read_idx:read_idx + 8 * 8 * 4]
            tile_pixels = np.zeros((8, 8, 4))
            ind = 0

            tile_pixels = np.array(self.readbuf[read_idx:read_idx + 8 * 8 * 4], copy=True, dtype=np.uint8).reshape((8, 8, 4))

            #import matplotlib.pyplot as plt
            # plt.imshow(tile_pixels[:,:,[2,1,0]],interpolation='none')
            # plt.show()

            read_idx += 8 * 8 * 4
            new_tiles.append(Tile(tile_hash, tile_idx, tile_pal, tile_pixels))
        return new_tiles

    def step(self, move_gen, infos):
        # type: (Iterable[Iterable[int]], Infos) -> Tuple[Iterable[PerFrame], Summary]
        move_gens = list(move_gen)
        assert len(move_gens) == self.num_players
        moves = map(lambda m: list(m), move_gens)  # type: List[List[int]]
        move_count = len(moves[0])
        move_bytes = move_count * self.num_players * self.bytes_per_player
        assert move_bytes < (len(self.writebuf) - 6)
        assert move_count < 2**16
        for move_list in moves:
            assert len(move_list) == move_count
        self.writebuf[0] = chr(CmdStep)
        self.writebuf[1] = chr(infos_to_byte(infos))
        self.writebuf[2] = chr(self.num_players)
        self.writebuf[3] = chr(self.bytes_per_player)
        self.writebuf[4:6] = to_uint16(move_count)
        for i in range(move_count):
            for p in range(self.num_players):
                # TODO: for each byte of the move
                self.writebuf[6 + i * self.num_players * self.bytes_per_player + p * self.bytes_per_player] = chr(moves[p][i])
        self.inp.write(cast(bytearray, self.writebuf[:6 + move_bytes]))
        self.inp.flush()
        # read outputs from self.outp
        per_frames = []
        read_idx = 0
        for m in range(move_count):
            framebuffer = None  # type: Optional[np.ndarray[int]]
            tiles_by_pixel = None  # type: Optional[List[List[PixelTileData]]]
            live_sprites = None  # type: Optional[List[Sprite]]
            if infos.framebuffer:
                assert self.outp.readinto(cast(bytearray, self.readbuf[:self.framebuffer_length])) == self.framebuffer_length
                read_idx = 0
                framebuffer = cast(np.ndarray, np.array(self.readbuf[:self.framebuffer_length], copy=True, dtype=np.uint8).reshape((self.framebuffer_height, self.framebuffer_width, self.framebuffer_depth)))
            if infos.tiles_by_pixel:
                read_idx = 0
                assert self.outp.readinto(cast(bytearray, self.readbuf[:(self.framebuffer_height * self.framebuffer_width * 6)])) == (self.framebuffer_height * self.framebuffer_width * 6)
                tiles_by_pixel = []
                for x in range(self.framebuffer_width):
                    tiles_by_pixel.append([])
                    for y in range(self.framebuffer_height):
                        hash = from_uint32(self.readbuf[read_idx:read_idx + 4])
                        xscroll = ord(self.readbuf[read_idx + 4])
                        yscroll = ord(self.readbuf[read_idx + 5])
                        read_idx += 4 + 1 + 1
                        tiles_by_pixel[-1].append(PixelTileData(hash, xscroll, yscroll))
            if infos.live_sprites:
                live_sprites = []
                assert self.outp.readinto(cast(bytearray, self.readbuf[:4])) == 4
                how_many = from_uint32(self.readbuf[0:4])
                print("SPR", how_many, list(self.readbuf[0:4]))
                if how_many != 0:
                    assert self.outp.readinto(cast(bytearray, self.readbuf[:how_many * 7])) == how_many * 7
                    read_idx = 0
                    print("A")
                    for sprite_idx in range(how_many):
                        sprite_hash = from_uint32(self.readbuf[read_idx:read_idx + 4])
                        sprite_flags = ord(self.readbuf[read_idx + 4])
                        hori = sprite_flags & (1 << 2)
                        vert = sprite_flags & (1 << 1)
                        background = sprite_flags & (1 << 0)
                        sprite_x = ord(self.readbuf[read_idx + 5])
                        sprite_y = ord(self.readbuf[read_idx + 6])
                        read_idx += 4 + 1 + 1 + 1
                        live_sprites.append(Sprite(sprite_hash, hori, vert, background, sprite_x, sprite_y))
            per_frames.append(PerFrame(framebuffer, tiles_by_pixel, live_sprites))
        # read summary statistics if infos have them
        print("done")
        new_tiles = None  # type: Optional[List[Tile]]
        new_sprite_tiles = None  # type: Optional[List[Tile]]
        if infos.new_tiles:
            new_tiles = self.read_tile_sequence()
        if infos.new_sprite_tiles:
            print('sprite tiles')
            new_sprite_tiles = self.read_tile_sequence()
        print("wait")
        self.wait_ready()
        print("ready")
        summary = Summary(new_tiles, new_sprite_tiles)
        return (per_frames, summary)
