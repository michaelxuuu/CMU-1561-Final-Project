DIR = src
SRC = $(wildcard $(DIR)/*.c)
APP = app
SUFFIX = 2>/dev/null || true
PREFIX = -@
OUT = out
INIT = .gdbinit
CC = clang
CCFLAGS = -g

.PHONY: clean

gdb: $(APP)
	$(PREFIX) gdb -x .gdbinit $<

run: $(APP)
	$(PREFIX)./$<

$(APP): $(SRC)
	$(PREFIX)$(CC) $(CCFLAGS) $^ -o $@

clean:
	$(PREFIX)rm $(APP) $(OUT) $(SUFFIX)
	$(PREFIX)rm -rf $(APP).dSYM $(SUFFIX)
