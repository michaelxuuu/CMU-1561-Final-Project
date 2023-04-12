DIR = src
SRC = $(wildcard $(DIR)/*.c)
APP = app

debug: $(APP)
	lldb $<

run: $(APP)
	./$<

$(APP): $(SRC)
	gcc $^ -o $@ -g

clean:
	rm $(APP)
