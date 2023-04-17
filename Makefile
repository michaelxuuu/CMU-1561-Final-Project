DIR = src
SRC = $(wildcard $(DIR)/*.c $(DIR)/asm.s)
OBJ = $(SRC:.c=.o)
APP = app
SUFFIX = 2>/dev/null || true
PREFIX = -@
OUT = out
INIT = .gdbinit
CC = clang
CCFLAGS = -g

.PHONY: clean

gdb: $(APP)
	gdb -x .gdbinit $<

run: $(APP)
	./$<

$(APP): $(OBJ)
	$(CC) $(CCFLAGS) $^ -o $@

$(DIR)/%.o : $(DIR)/%.c
	$(CC) $(CCFLAGS) $< -c -o $@

clean:
	$(PREFIX)	rm $(APP) $(OUT) 	$(SUFFIX)
	$(PREFIX)	rm -rf $(APP).dSYM 	$(SUFFIX)
	$(PREFIX)	rm $(DIR)/*.o 		$(SUFFIX)
