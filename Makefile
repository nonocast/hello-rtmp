CC=clang
ARCH=x86_64
CFLAGS=`pkg-config --cflags librtmp`
LDFLAGS=`pkg-config --libs librtmp`
SRC=src
BUILD=build
PROG=$(BUILD)/dump $(BUILD)/parser

all: $(BUILD) $(PROG)

$(BUILD)/dump: $(SRC)/dump.c
	@$(CC) -arch $(ARCH) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD)/parser: $(SRC)/parser.c
	@$(CC) -arch $(ARCH) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD):
	@mkdir -p $@

run-dump: $(BUILD)/dump
	$(BUILD)/dump -o out.flv rtmp://shgbit.xyz/live/1

run-parser: $(BUILD)/parser
	@$(BUILD)/parser out.flv

# alias
run: run-parser

clean:
	@rm -rf build

.PHONY: all clean