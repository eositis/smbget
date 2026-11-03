# SMBGET - build with cc65 (brew install cc65)
CL65   = cl65
TARGET = apple2enh
IP65   = ext/ip65
CFLAGS = -t $(TARGET) -Os -DSINGLE_SOCKET -I$(IP65)/inc --asm-include-dir $(IP65)/inc
TCPOBJ = build/ethdrv.o
LIBS   = $(IP65)/ip65/ip65_tcp.lib $(IP65)/drivers/ip65_apple2_uther2.lib
SRC    = src/main.c src/ui.c src/pane.c src/prodos.c src/copy.c src/names.c src/net.c src/w5100.c src/smb2.c src/mli.s src/auxmem.s src/ip65_outbuf.s src/eth_buffer.s src/ip65_icmp.s src/ip65_error.s src/tcp_stub.s
OUT    = build/SMBGET.SYSTEM

all: $(OUT) build/SMBGET.OVL build/smbget.po

# BUILDNUM holds the number the *next* build will show; it is bumped after each build.
build/ethdrv.o: src/ethdrv.s | build
	ca65 -t apple2enh -I$(IP65)/inc -o $@ $<

$(OUT): cfg/smbget.cfg $(TCPOBJ) $(SRC) $(filter-out src/build.h,$(wildcard src/*.h)) | build
	@n=$$(cat BUILDNUM); printf '#define BUILD_ID "b%03d"\n' $$n > src/build.h; echo "building b$$(printf %03d $$n)"
	$(CL65) $(CFLAGS) -C cfg/smbget.cfg -Wl -D,__STACKSIZE__=0x0200 -m build/smbget.map -Ln build/smbget.lbl -o $@ $(SRC) $(TCPOBJ) $(LIBS)
	@echo $$(( $$(cat BUILDNUM) + 1 )) > BUILDNUM

# Optional: a real ProDOS 140K .po whose boot blocks 0-1 get copied into our image
# (make BOOTREF=/path/to/any.po). Defaults to the megaflash repo's FLASHVALID.po if present.
BOOTREF ?= $(firstword $(wildcard ../megaflash/tools/flash-validate/FLASHVALID.po))

# ---- BASIC conversion overlay: SMBGET.OVL, linked for $0800 and sharing the main program's zero page ----
CC65_HOME := $(shell python3 -c "import os,shutil;print(os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(shutil.which('cl65')))),'share','cc65'))")
OVLSRC = src/ovl/ovl.c src/ovl/bas.c

build/SMBGET.OVL: $(OUT) $(OVLSRC) src/ovl/ovl.h src/ovl/bas.h src/ovl/bas_rom.s src/ovl/ovlstart.s cfg/ovl.cfg tools/mkzp.py
	python3 tools/mkzp.py build/smbget.lbl build/zpsyms.s
	ca65 -t apple2enh -o build/zpsyms.o build/zpsyms.s
	ca65 -t apple2enh -o build/ovlstart.o src/ovl/ovlstart.s
	ca65 -t apple2enh -o build/bas_rom.o src/ovl/bas_rom.s
	cl65 -t apple2enh -Os -c -o build/ovl_c.o src/ovl/ovl.c
	cl65 -t apple2enh -Os -c -o build/bas_c.o src/ovl/bas.c
	ld65 -C cfg/ovl.cfg -m build/ovl.map -o $@ build/ovlstart.o build/ovl_c.o build/bas_c.o build/bas_rom.o build/zpsyms.o $(CC65_HOME)/lib/apple2enh.lib
	@echo "overlay: $$(wc -c < $@) bytes"

# Files to put on the SMB share (or any host folder). The names alone tell SMBGET what they are:
# .system -> ProDOS SYS $FF, aux $2000; .ovl -> BIN, aux $0800. Contents are the exact bytes for the Apple II.
dist: $(OUT) build/SMBGET.OVL
	mkdir -p dist
	cp $(OUT) dist/smbget.system
	cp build/SMBGET.OVL dist/smbget.ovl
	@ls -l dist

build/smbget.po: $(OUT) build/SMBGET.OVL tools/mkpo.py
	python3 tools/mkpo.py $@ $(OUT) $(BOOTREF)
	python3 tools/mkpo.py add $@ build/SMBGET.OVL --name SMBGET.OVL --type 06 --aux 0800

build:
	mkdir -p build

test:
	$(MAKE) -C tests/host test
	python3 tools/check_cc65_bug.py -t $(TARGET) -Os -DSINGLE_SOCKET -I$(IP65)/inc -- $(filter %.c,$(SRC)) src/ovl/ovl.c src/ovl/bas.c

clean:
	rm -rf build
	rm -f src/*.o
	$(MAKE) -C tests/host clean
