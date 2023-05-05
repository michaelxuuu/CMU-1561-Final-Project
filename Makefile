ASM	= $(wildcard *.s)
SRC	= $(wildcard *.c)
OBJ	= $(SRC:%.c=%.o) $(ASM:%.s=%.o)
CC 	= gcc
CCFLAGS = -g

APP	= app
PRE	= -@
SUF	= 2>/dev/null || true

run: $(APP)
	./$<

gdb: $(APP)
	gdb -x .gdbinit $<

$(APP): $(OBJ)
	$(CC) $(CCFLAGS) $^ -o $@

%.o : %.c
	$(CC) $(CCFLAGS) $< -c -o $@

clean:
	$(PRE) rm $(APP) $(OBJ) $(SUF)
