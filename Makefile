DIR = src
SRC = $(wildcard $(DIR)/*.c)
APP = app
SUFFIX = 2>/dev/null || true
PREFIX = -@
OUT = out

.PHONY: clean

debug: $(APP)
	$(PREFIX)lldb $< $(SUFFIX)

test_lf_insert: $(APP)
	$(PREFIX) ./$< > $(OUT) $(SUFFIX)
	$(PREFIX)python test.py $(SUFFIX)

run: $(APP)
	$(PREFIX)./$< $(SUFFIX)

$(APP): $(SRC)
	$(PREFIX)gcc $^ -o $@ -g $(SUFFIX)

clean:
	$(PREFIX)rm $(APP) $(OUT) $(SUFFIX)
	$(PREFIX)rm -rf $(APP).dSYM $(SUFFIX)
