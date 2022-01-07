BIN = xs-timeout

X11_CFLAGS ?= $(shell pkg-config --cflags x11 xext)
X11_LDFLAGS ?= $(shell pkg-config --libs x11 xext)

CFLAGS += -Wall -Wextra -pedantic -pedantic-errors -std=c99 -D_POSIX_C_SOURCE=200112 -D_GNU_SOURCE

OBJECTS = src/main.o src/daemon.o src/timeouts.o src/options.o

all: $(BIN)

clangd: compile_flags.txt

compile_flags.txt:
	@echo $(CFLAGS) -I./includes $(X11_CFLAGS) | tr ' ' '\n' > compile_flags.txt

%.o: %.c
	@echo CC $@
	@$(CC) $(CFLAGS) -I./includes $(X11_CFLAGS) -c -o $@ $<

$(BIN): $(OBJECTS)
	@echo LD $(BIN)
	@$(CC) $(CFLAGS) -I./includes $(X11_CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LDFLAGS)

clean:
	@echo CLEAN
	@rm -rf $(OBJECTS) $(BIN)

deep_clean: clean
	@rm -rf compile_flags.txt compile_commands.json

valgrind:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s

.PHONY: clean all clangd valgrind