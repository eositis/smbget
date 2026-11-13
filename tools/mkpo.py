#!/usr/bin/env python3
"""Minimal ProDOS .po image tool (no external dependencies).

  mkpo.py new IMAGE [--name VOL] [--blocks N]     create an empty volume
  mkpo.py add IMAGE FILE [--name NAME] [--type HEX] [--aux HEX]   add to the root directory
  mkpo.py ls  IMAGE                                list the root directory
  mkpo.py IMAGE SYSTEMFILE [BOOTREF]              new image holding SMBGET.SYSTEM. BOOTREF is a
                                                   140K .po or DOS-order .dsk; its loader (blocks 0-1)
                                                   and PRODOS file are copied so the image boots.

Files up to 128 KB (seedling/sapling) are supported.
"""
import argparse, datetime, os, struct, sys

BLK = 512


def strip_applesingle(data):
    """cl65 emits an AppleSingle wrapper for Apple II targets; return the data fork only."""
    if data[:4] != b"\x00\x05\x16\x00":
        return data
    n = struct.unpack_from(">H", data, 24)[0]
    for i in range(n):
        eid, off, ln = struct.unpack_from(">III", data, 26 + i * 12)
        if eid == 1:
            return data[off:off + ln]
    raise SystemExit("AppleSingle file has no data fork")


def dt_word(now=None):
    n = now or datetime.datetime.now()
    return ((n.year % 100) << 9) | (n.month << 5) | n.day, (n.hour << 8) | n.minute


class Vol:
    def __init__(self, data):
        self.d = bytearray(data)
        h = self.blk(2)
        self.total = struct.unpack_from("<H", h, 4 + 37)[0]
        self.bitmap = struct.unpack_from("<H", h, 4 + 35)[0]

    def blk(self, n):
        return memoryview(self.d)[n * BLK:(n + 1) * BLK]

    # --- allocation: bit set = free, block 0 = MSB of byte 0 -------------
    def _bm_nblocks(self):
        return (self.total + BLK * 8 - 1) // (BLK * 8)

    def is_free(self, n):
        b = self.d[self.bitmap * BLK + n // 8]
        return bool(b & (0x80 >> (n % 8)))

    def set_free(self, n, free):
        i = self.bitmap * BLK + n // 8
        m = 0x80 >> (n % 8)
        self.d[i] = (self.d[i] | m) if free else (self.d[i] & ~m & 0xFF)

    def alloc(self):
        for n in range(self.total):
            if self.is_free(n):
                self.set_free(n, False)
                return n
        raise SystemExit("disk full")

    def free_count(self):
        return sum(self.is_free(n) for n in range(self.total))

    # --- directory ----------------------------------------------------------
    def dir_blocks(self):
        n, out = 2, []
        while n:
            out.append(n)
            n = struct.unpack_from("<H", self.blk(n), 2)[0]
        return out

    def entries(self):
        for bi, n in enumerate(self.dir_blocks()):
            b = self.blk(n)
            for i in range(13):
                off = 4 + i * 39
                if bi == 0 and i == 0:
                    continue
                yield n, off, b[off:off + 39]

    def find_slot(self):
        for n, off, e in self.entries():
            if e[0] >> 4 == 0:
                return n, off
        raise SystemExit("root directory full")

    def bump_count(self):
        h = self.blk(2)
        c = struct.unpack_from("<H", h, 4 + 33)[0]
        struct.pack_into("<H", h, 4 + 33, c + 1)

    def remove(self, name):
        """Delete a root-directory file if present (frees its blocks)."""
        for n, off, e in self.entries():
            if e[0] >> 4 and bytes(e[1:1 + (e[0] & 15)]).decode() == name:
                st = e[0] >> 4
                key = struct.unpack_from("<H", e, 17)[0]
                if st == 1:
                    self.set_free(key, True)
                elif st == 2:
                    idx = bytes(self.blk(key))
                    for i in range(256):
                        b = idx[i] | idx[256 + i] << 8
                        if b:
                            self.set_free(b, True)
                    self.set_free(key, True)
                else:
                    raise SystemExit("cannot replace %s (storage type %d)" % (name, st))
                self.blk(n)[off] = 0
                h = self.blk(2)
                c = struct.unpack_from("<H", h, 4 + 33)[0]
                struct.pack_into("<H", h, 4 + 33, c - 1)
                return

    def read_file(self, name):
        """Return the bytes of a root-directory file, or None."""
        name = name.upper()
        for _, _, e in self.entries():
            if e[0] >> 4 and bytes(e[1:1 + (e[0] & 15)]).decode() == name:
                st = e[0] >> 4
                key = struct.unpack_from("<H", e, 17)[0]
                eof = int.from_bytes(e[21:24], "little")
                if st == 1:
                    return bytes(self.blk(key)[:eof])
                if st == 2:
                    idx = bytes(self.blk(key))
                    out = bytearray()
                    for i in range(256):
                        b = idx[i] | (idx[256 + i] << 8)
                        if not b:
                            break
                        out += self.blk(b)
                    return bytes(out[:eof])
                raise SystemExit("cannot read %s (storage type %d)" % (name, st))
        return None

    def add(self, name, data, ftype, aux):
        name = name.upper()
        if not (1 <= len(name) <= 15):
            raise SystemExit("bad name")
        self.remove(name)
        nblk = max(1, (len(data) + BLK - 1) // BLK)
        if nblk > 256:
            raise SystemExit("file too large (max 128 KB)")
        if nblk == 1:
            storage, blocks, key = 1, 1, self.alloc()
            self.blk(key)[:len(data)] = data
        else:
            storage, key = 2, self.alloc()
            idx = self.blk(key)
            for i in range(nblk):
                b = self.alloc()
                chunk = data[i * BLK:(i + 1) * BLK]
                self.blk(b)[:len(chunk)] = chunk
                idx[i] = b & 255
                idx[256 + i] = b >> 8
            blocks = nblk + 1
        n, off = self.find_slot()
        date, time = dt_word()
        e = bytearray(39)
        e[0] = (storage << 4) | len(name)
        e[1:1 + len(name)] = name.encode()
        e[16] = ftype
        struct.pack_into("<H", e, 17, key)
        struct.pack_into("<H", e, 19, blocks)
        e[21:24] = struct.pack("<I", len(data))[:3]
        struct.pack_into("<HH", e, 24, date, time)
        e[30] = 0xE3
        struct.pack_into("<H", e, 31, aux)
        struct.pack_into("<HH", e, 33, date, time)
        struct.pack_into("<H", e, 37, 2)
        self.blk(n)[off:off + 39] = e
        self.bump_count()


# DOS 3.3 logical sector -> ProDOS block-order sector, within a track.
DOS_TO_PO = [0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15]


def is_prodos_volume(img):
    if len(img) < 3 * BLK:
        return False
    hdr = img[2 * BLK + 4]
    entry_len = img[2 * BLK + 4 + 0x1F]
    return (hdr & 0xF0) == 0xF0 and entry_len == 0x27


def dos_to_po(src):
    out = bytearray(280 * BLK)
    for block in range(280):
        track = block // 8
        for half in (0, 1):
            sec = DOS_TO_PO[(block % 8) * 2 + half]
            src_off = (track * 16 + sec) * 256
            out[block * BLK + half * 256:block * BLK + half * 256 + 256] = src[src_off:src_off + 256]
    return out


def load_reference(path):
    """Return a 140K image in ProDOS block order."""
    src = open(path, "rb").read()
    if len(src) < 280 * BLK:
        raise SystemExit("%s is not a 140K disk image" % path)
    src = src[:280 * BLK]
    if is_prodos_volume(src):
        return src
    converted = dos_to_po(src)
    if is_prodos_volume(converted):
        return converted
    raise SystemExit("%s has no ProDOS volume directory" % path)


def real_loader(boot):
    """AppleCommander writes a splash into block 0 that never loads PRODOS."""
    plain = bytes(b & 0x7F for b in boot[:2 * BLK])
    return b"UNABLE TO LOAD PRODOS" in plain


def new_volume(name, blocks):
    d = bytearray(blocks * BLK)
    v = Vol.__new__(Vol)
    v.d, v.total, v.bitmap = d, blocks, 6
    nbm = v._bm_nblocks()
    for n in range(blocks):
        v.set_free(n, True)
    for n in range(0, 6 + nbm):
        v.set_free(n, False)
    for i, n in enumerate((2, 3, 4, 5)):
        b = v.blk(n)
        struct.pack_into("<HH", b, 0, 0 if i == 0 else n - 1, n + 1 if i < 3 else 0)
    h = v.blk(2)
    date, time = dt_word()
    name = name.upper()
    h[4] = 0xF0 | len(name)
    h[5:5 + len(name)] = name.encode()
    struct.pack_into("<HH", h, 4 + 24, date, time)
    h[4 + 30] = 0xE3
    h[4 + 31] = 0x27
    h[4 + 32] = 0x0D
    struct.pack_into("<HHH", h, 4 + 33, 0, 6, blocks)
    return Vol(v.d)


def ls(v):
    h = v.blk(2)
    print("/%s  %d blocks, %d free" % (bytes(h[5:5 + (h[4] & 15)]).decode(), v.total, v.free_count()))
    for _, _, e in v.entries():
        if e[0] >> 4:
            nm = bytes(e[1:1 + (e[0] & 15)]).decode()
            print("  %-15s type $%02X aux $%04X  %5d bytes  %3d blks" % (
                nm, e[16], struct.unpack_from("<H", e, 31)[0],
                int.from_bytes(e[21:24], "little"), struct.unpack_from("<H", e, 19)[0]))


def main():
    a = sys.argv[1:]
    if len(a) in (2, 3) and not a[0] in ("new", "add", "ls"):  # shortcut: IMAGE SYSTEMFILE [BOOTREF]
        v = new_volume("SMBGET", 280)
        note = "copy it onto a disk that already has ProDOS"
        if len(a) == 3:
            ref = load_reference(a[2])
            prodos = Vol(ref).read_file("PRODOS")
            if real_loader(ref) and prodos:
                v.d[:2 * BLK] = ref[:2 * BLK]
                v.add("PRODOS", prodos, 0xFF, 0x2000)
                note = "boots into the first .SYSTEM file"
            elif prodos:
                v.add("PRODOS", prodos, 0xFF, 0x2000)
                note = "PRODOS copied, but the reference boot block is not a loader"
            else:
                note = "reference disk has no PRODOS file"
        v.add("SMBGET.SYSTEM", strip_applesingle(open(a[1], "rb").read()), 0xFF, 0x2000)
        open(a[0], "wb").write(v.d)
        print("wrote %s (%s)" % (a[0], note))
        return
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("new"); p.add_argument("image"); p.add_argument("--name", default="NEWVOL"); p.add_argument("--blocks", type=int, default=280)
    p = sp.add_parser("add"); p.add_argument("image"); p.add_argument("file"); p.add_argument("--name"); p.add_argument("--type", default="06"); p.add_argument("--aux", default="0800")
    p = sp.add_parser("ls"); p.add_argument("image")
    o = ap.parse_args(a)
    if o.cmd == "new":
        open(o.image, "wb").write(new_volume(o.name, o.blocks).d)
    elif o.cmd == "add":
        v = Vol(open(o.image, "rb").read())
        v.add(o.name or os.path.basename(o.file), strip_applesingle(open(o.file, "rb").read()), int(o.type, 16), int(o.aux, 16))
        open(o.image, "wb").write(v.d)
    else:
        ls(Vol(open(o.image, "rb").read()))


if __name__ == "__main__":
    main()
