CC     = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -g

OBJS  = main.o device.o library.o database.o config.o tags.o textconv.o
TESTS = test_device test_library test_database test_config test_tags test_copier test_mp3info test_dbwriter
TOOLS = copier_demo dbwrite_demo

sync: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

test_device:   test_device.o device.o
test_library:  test_library.o library.o tags.o textconv.o
test_database: test_database.o database.o textconv.o
test_config:   test_config.o config.o
test_tags:     test_tags.o tags.o textconv.o
test_copier:   test_copier.o copier.o
test_mp3info:  test_mp3info.o mp3info.o
test_dbwriter: test_dbwriter.o dbwriter.o database.o textconv.o

copier_demo:   copier_demo.o copier.o
dbwrite_demo:  dbwrite_demo.o dbwriter.o database.o mp3info.o tags.o textconv.o

$(TESTS) $(TOOLS):
	$(CC) $(CFLAGS) -o $@ $^

tools: $(TOOLS)

test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done

%.o: %.c $(wildcard *.h)
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o sync $(TESTS) $(TOOLS)

.PHONY: test tools clean
