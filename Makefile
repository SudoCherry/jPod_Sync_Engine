# jPod Sync Engine
#
# Layout:  src/ (program code)   include/ (headers)   tests/ (test programs)
#          tools/ (small helper programs)   build/ (object files)   bin/ (finished programs)
#
#   make          build bin/sync
#   make test     build and run every test
#   make tools    build the helper programs (copier_demo, dbwrite_demo)
#   make clean    delete build/ and the programs in bin/

CC       = gcc
CPPFLAGS = -Iinclude -Isrc
CFLAGS   = -Wall -Wextra -pedantic -std=c11 -g

BUILD = build
BIN   = bin

# make looks for each .c file in these folders
vpath %.c src tests tools

HEADERS = $(wildcard include/*.h src/*.h tests/*.h)

obj = $(addprefix $(BUILD)/,$(addsuffix .o,$1))

TESTS = test_device test_library test_database test_config test_tags test_copier test_mp3info test_dbwriter
TOOLS = copier_demo dbwrite_demo
TEST_BINS = $(addprefix $(BIN)/,$(TESTS))
TOOL_BINS = $(addprefix $(BIN)/,$(TOOLS))

all: $(BIN)/sync

$(BIN)/sync: $(call obj,main device library database config tags textconv)

$(BIN)/test_device:   $(call obj,test_device device)
$(BIN)/test_library:  $(call obj,test_library library tags textconv)
$(BIN)/test_database: $(call obj,test_database database textconv)
$(BIN)/test_config:   $(call obj,test_config config)
$(BIN)/test_tags:     $(call obj,test_tags tags textconv)
$(BIN)/test_copier:   $(call obj,test_copier copier)
$(BIN)/test_mp3info:  $(call obj,test_mp3info mp3info)
$(BIN)/test_dbwriter: $(call obj,test_dbwriter dbwriter database textconv)

$(BIN)/copier_demo:   $(call obj,copier_demo copier)
$(BIN)/dbwrite_demo:  $(call obj,dbwrite_demo dbwriter database mp3info tags textconv)

# every program is linked the same way
$(BIN)/sync $(TEST_BINS) $(TOOL_BINS): | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/%.o: %.c $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD) $(BIN):
	mkdir -p $@

tools: $(TOOL_BINS)

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do ./$$t || exit 1; done

clean:
	rm -rf $(BUILD)
	rm -f $(BIN)/sync $(TEST_BINS) $(TOOL_BINS)

.PHONY: all tools test clean
