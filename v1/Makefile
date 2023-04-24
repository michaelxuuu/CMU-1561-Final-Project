SRC1 = $(wildcard **/*.c)
SRC2 = $(wildcard **/*.s)
OBJ = $(SRC1:%.c=%.o) $(SRC2:%.s=%.o)
CC = gcc
CCFLAGS = -g

APP = app
PREFIX = -@
SUFFIX = 2>/dev/null || true

gdb: $(APP)
	gdb -x .gdbinit $<

run: $(APP)
	./$<

$(APP): $(OBJ)
	$(CC) $(CCFLAGS) $^ -o $@

%.o : %.c
	$(CC) $(CCFLAGS) $< -c -o $@

clean:
	$(PREFIX) rm $(APP) $(OBJ)   $(SUFFIX)
	$(PREFIX) rm -rf $(APP).dSYM $(SUFFIX)
