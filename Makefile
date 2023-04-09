DIR = src
SRC = $(wildcard $(DIR)/*.c)
APP = app
MSG = 

debug: $(APP)
	sudo gdb -x .gdbinit $<

run: $(APP)
	./$<

$(APP): $(SRC)
	gcc $^ -o $@ -g

clean:
	rm $(APP)
