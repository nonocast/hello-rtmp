CC=clang
ARCH=x86_64
CFLAGS=`pkg-config --cflags librtmp`
LDFLAGS=`pkg-config --libs librtmp`
SRC=src
BUILD=build
PROG=$(BUILD)/dump $(BUILD)/parser $(BUILD)/client $(BUILD)/test-amf $(BUILD)/replay

all: $(BUILD) $(PROG)

$(BUILD)/dump: $(SRC)/dump.c
	@$(CC) -arch $(ARCH) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD)/parser: $(SRC)/parser.c
	@$(CC) -arch $(ARCH) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD)/client: $(SRC)/client.c
	@$(CC) -arch $(ARCH) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD)/test-amf: $(SRC)/test-amf.c
	@$(CC) -arch $(ARCH) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD)/replay: $(SRC)/replay.c
	@$(CC) -arch $(ARCH) $(CFLAGS) $(LDFLAGS) -o $@ $<

$(BUILD):
	@mkdir -p $@

run-dump: $(BUILD)/dump
	$(BUILD)/dump -o out.flv rtmp://shgbit.xyz/live/1

run-parser: $(BUILD)/parser
	@$(BUILD)/parser out.flv

run-client: $(BUILD)/client
	@$(BUILD)/client

run-test-amf: $(BUILD)/test-amf
	@$(BUILD)/test-amf

run-replay: $(BUILD)/replay
	@$(BUILD)/replay out.flv

# alias
run: run-replay

clean:
	@rm -rf build

.PHONY: all clean