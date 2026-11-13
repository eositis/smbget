# SMBGET - build with cc65 (brew install cc65)
CL65   = cl65
TARGET = apple2enh
IP65   = ext/ip65
CFLAGS = -t $(TARGET) -Os -DSINGLE_SOCKET -I$(IP65)/inc --asm-include-dir $(IP65)/inc
IP65LIB = $(IP65)/ip65/ip65_tcp.lib
A2DRV   = $(IP65)/drivers/ip65_apple2_uther2.lib
SRC    = src/main.c src/ui.c src/pane.c src/prodos.c src/copy.c src/net.c src/nic.c src/slot.c src/smb2.c src/mli.s src/auxmem.s src/ip65_outbuf.s src/eth_pad.s src/eth_buffer.s src/mfid.s src/timer.s src/names_lc.s src/ramunhook.s
OUT    = build/SMBGET.SYSTEM
NIC_HOLE = 0x1800

all: $(OUT) build/SMBGET.OVL build/SMBGET.UTH build/SMBGET.COG build/SMBGET.NAM build/smbget.po

# BUILDNUM holds the number the *next* build will show; it is bumped after each build.
$(OUT): cfg/smbget.cfg $(SRC) $(filter-out src/build.h,$(wildcard src/*.h)) src/ovl/ovl.h | build
	@n=$$(cat BUILDNUM); printf '#define BUILD_ID "b%03d"\n' $$n > src/build.h; echo "building b$$(printf %03d $$n)"
	$(CL65) $(CFLAGS) -C cfg/smbget.cfg -Wl -D,__STACKSIZE__=0x0200 -m build/smbget.map -Ln build/smbget.lbl -o $@ $(SRC)
	@python3 -c 'import re;lbl=open("build/smbget.lbl").read();sym=lambda n:int(re.search(r"al +([0-9A-Fa-f]+) +\."+n+r"\b",lbl).group(1),16);sizes=[int(x) for x in re.findall(r"\.res +(\d+)",open("src/eth_buffer.s").read())];inp,outp,inn,outn=sym("_eth_inp"),sym("_eth_outp"),sizes[0],sizes[1];end=outp+outn;aligned=(inp+255)&~255;bss=sym("__BSS_RUN__")+sym("__BSS_SIZE__");ovl=int(re.search(r"OVL_ADDR +0x([0-9A-Fa-f]+)",open("src/ovl/ovl.h").read()).group(1),16);assert outp==inp+inn,"ethernet buffers are not contiguous";assert aligned+1024<=end,"page-aligned 1K ProDOS buffer %04X..%04X overruns ethernet buffers ending %04X"%(aligned,aligned+1023,end-1);assert bss<=ovl,"main BSS ends at %04X, which overlaps the BASIC overlay at %04X"%(bss,ovl);print("layout: eth %04X-%04X, 1K at %04X, BSS ends %04X, overlay %04X"%(inp,end-1,aligned,bss-1,ovl))'
	@echo $$(( $$(cat BUILDNUM) + 1 )) > BUILDNUM

# A 140K ProDOS image (.po, or a DOS-order .dsk) that supplies the loader and PRODOS.
# FLASHVALID.po is an AppleCommander image: its block 0 is a splash and will not boot.
BOOTREF ?= $(firstword $(wildcard ../megaflash/cpanel/prodos19.dsk ../megaflash/tools/flash-validate/FLASHVALID.po))

# ---- BASIC conversion overlay: SMBGET.OVL, linked above the main BSS, sharing its zero page ----
CC65_HOME := $(shell python3 -c "import os,shutil;print(os.path.join(os.path.dirname(os.path.dirname(os.path.realpath(shutil.which('cl65')))),'share','cc65'))")
OVLSRC = src/ovl/ovl.c src/ovl/bas.c

build/SMBGET.OVL: $(OUT) $(OVLSRC) src/ovl/ovl.h src/ovl/bas.h src/ovl/bas_rom.s src/ovl/ovlstart.s tools/mkzp.py tools/linkbas.py
	python3 tools/mkzp.py build/smbget.lbl build/zpsyms.s
	ca65 -t apple2enh -o build/zpsyms.o build/zpsyms.s
	ca65 -t apple2enh -o build/ovlstart.o src/ovl/ovlstart.s
	ca65 -t apple2enh -o build/bas_rom.o src/ovl/bas_rom.s
	cl65 -t apple2enh -Os -c -o build/ovl_c.o src/ovl/ovl.c
	cl65 -t apple2enh -Os -c -o build/bas_c.o src/ovl/bas.c
	python3 tools/linkbas.py build/smbget.lbl $@ build/ovl.map build/ovl.cfg -- build/ovlstart.o build/ovl_c.o build/bas_c.o build/bas_rom.o build/zpsyms.o $(CC65_HOME)/lib/apple2enh.lib

# NIC overlays, linked at nic_hole. UTH is Uthernet II + IP65. COG is CoGS.
CC65LIB = $(CC65_HOME)/lib/apple2enh.lib
UTH_OBJS = build/uth_vec.o build/nic_uth.o build/w5100.o build/w5100drv.o build/timer_uth.o build/ethdrv.o build/tcp_stub.o build/ip65_icmp.o build/ip65_error.o
COG_OBJS = build/cog_vec.o build/nic_cog.o build/cogs.o build/cogs_io.o

build/ethdrv.o: src/ethdrv.s | build
	ca65 -t apple2enh -I$(IP65)/inc -o $@ $<

build/tcp_stub.o: src/tcp_stub.s | build
	ca65 -t apple2enh -o $@ $<

build/ip65_icmp.o: src/ip65_icmp.s | build
	ca65 -t apple2enh -I$(IP65)/inc -o $@ $<

build/ip65_error.o: src/ip65_error.s | build
	ca65 -t apple2enh -I$(IP65)/inc -o $@ $<

build/uth_vec.o: src/uth_vec.s | build
	ca65 -t apple2enh -o $@ $<

build/timer_uth.o: src/timer_uth.s | build
	ca65 -t apple2enh -o $@ $<

build/w5100drv.o: ext/ip65/drivers/uthernet2.s ext/ip65/drivers/w5100.s | build
	ca65 -I$(IP65)/inc -I$(CC65_HOME)/asminc -o $@ $<

build/cog_vec.o: src/cog_vec.s | build
	ca65 -t apple2enh -o $@ $<

build/cogs_io.o: src/cogs_io.s | build
	ca65 -t apple2enh -o $@ $<

build/nic_uth.o: src/nic_uth.c src/net.h src/w5100.h | build
	$(CL65) -c -o $@ $(CFLAGS) $<

build/nic_cog.o: src/nic_cog.c src/net.h src/cogs.h src/w5100.h | build
	$(CL65) -c -o $@ $(CFLAGS) $<

build/w5100.o: src/w5100.c src/w5100.h | build
	$(CL65) -c -o $@ $(CFLAGS) $<

build/cogs.o: src/cogs.c src/cogs.h src/w5100.h src/net.h | build
	$(CL65) -c -o $@ $(CFLAGS) $<

build/SMBGET.UTH: $(OUT) $(UTH_OBJS) tools/mknic.py
	python3 tools/mknic.py build/smbget.lbl $(NIC_HOLE) $@ build/uth.map build/uth.cfg -- $(UTH_OBJS) $(IP65LIB) $(A2DRV) $(CC65LIB)

build/SMBGET.COG: $(OUT) $(COG_OBJS) tools/mknic.py
	python3 tools/mknic.py build/smbget.lbl $(NIC_HOLE) $@ build/cog.map build/cog.cfg -- $(COG_OBJS) $(CC65LIB)

# Name tables live in language-card bank 2 so the main image can hold the
# share operations. Loaded from SMBGET.NAM at startup (see load_names).
build/names.o: src/names.c src/names.h | build
	$(CL65) -c -o $@ $(CFLAGS) $<

build/tree.o: src/tree.c src/tree.h src/copy.h src/names.h | build
	$(CL65) -c -o $@ $(CFLAGS) $<

build/nam_vec.o: src/nam_vec.s | build
	ca65 -t apple2enh -o $@ $<

build/SMBGET.NAM: $(OUT) build/nam_vec.o build/names.o build/tree.o tools/linknam.py
	python3 tools/linknam.py build/smbget.lbl $@ build/nam.map build/nam.cfg -- build/nam_vec.o build/names.o build/tree.o $(CC65LIB)

# Files to put on the SMB share (or any host folder). The names alone tell SMBGET what they are:
# .system -> ProDOS SYS $FF, aux $2000; .ovl -> BIN, aux $0800. Contents are the exact bytes for the Apple II.
dist: $(OUT) build/SMBGET.OVL build/SMBGET.UTH build/SMBGET.COG build/SMBGET.NAM
	mkdir -p dist
	cp $(OUT) dist/smbget.system
	cp build/SMBGET.OVL dist/smbget.ovl
	cp build/SMBGET.UTH dist/smbget.uth
	cp build/SMBGET.COG dist/smbget.cog
	cp build/SMBGET.NAM dist/smbget.nam
	@ls -l dist

build/smbget.po: $(OUT) build/SMBGET.OVL build/SMBGET.UTH build/SMBGET.COG build/SMBGET.NAM tools/mkpo.py
	python3 tools/mkpo.py $@ $(OUT) $(BOOTREF)
	python3 tools/mkpo.py add $@ build/SMBGET.OVL --name SMBGET.OVL --type 06 --aux A930
	python3 tools/mkpo.py add $@ build/SMBGET.UTH --name SMBGET.UTH --type 06 --aux 0800
	python3 tools/mkpo.py add $@ build/SMBGET.COG --name SMBGET.COG --type 06 --aux 0800
	python3 tools/mkpo.py add $@ build/SMBGET.NAM --name SMBGET.NAM --type 06 --aux D400

build:
	mkdir -p build

test:
	$(MAKE) -C tests/host test
	python3 tools/check_cc65_bug.py -t $(TARGET) -Os -DSINGLE_SOCKET -I$(IP65)/inc -- $(filter %.c,$(SRC)) src/names.c src/tree.c src/ovl/ovl.c src/ovl/bas.c src/nic_uth.c src/nic_cog.c src/cogs.c

clean:
	rm -rf build
	rm -f src/*.o
	$(MAKE) -C tests/host clean
