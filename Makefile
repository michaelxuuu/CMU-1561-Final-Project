DIR = src
SRC = $(wildcard $(DIR)/*.c)
APP = app
SUFFIX = 2>/dev/null || true
PREFIX = -@
OUT = out
INIT = .lldbinit
CC = gcc
CCFLAGS = -g

.PHONY: clean

debug: $(APP)
	$(PREFIX)lldb -s $(INIT) $<

gdb: $(APP)
	$(PREFIX)sudo gdb -x .gdbinit $<


test_lf_insert: $(APP)
	$(PREFIX) ./$< > $(OUT)
	$(PREFIX)python test.py

run: $(APP)
	$(PREFIX)./$<

$(APP): $(SRC)
	$(PREFIX)$(CC) $(CCFLAGS) $^ -o $@

clean:
	$(PREFIX)rm $(APP) $(OUT) $(SUFFIX)
	$(PREFIX)rm -rf $(APP).dSYM $(SUFFIX)
