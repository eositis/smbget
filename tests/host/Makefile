CC ?= cc
CFLAGS = -Wall -Wextra -std=c99 -g
BUILD = build

test: $(BUILD)/test_names $(BUILD)/test_bas $(BUILD)/test_ovl
	$(BUILD)/test_names
	$(BUILD)/test_bas
	$(BUILD)/test_ovl >/dev/null && echo "overlay glue tests passed"

$(BUILD)/test_ovl: test_ovl.c ../../src/ovl/ovl.c ../../src/ovl/bas.c ../../src/ovl/ovl.h | $(BUILD)
	$(CC) -std=c99 -g -w -D__fastcall__= -I../../src -o $@ test_ovl.c ../../src/ovl/ovl.c ../../src/ovl/bas.c

$(BUILD)/test_bas: test_bas.c ../../src/ovl/bas.c ../../src/ovl/bas.h | $(BUILD)
	$(CC) -std=c99 -g -w -D__fastcall__= -o $@ test_bas.c ../../src/ovl/bas.c

$(BUILD)/test_names: test_names.c ../../src/names.c ../../src/names.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ test_names.c ../../src/names.c

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
