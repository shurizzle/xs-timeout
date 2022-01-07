BIN = xss-timeout

X11_CFLAGS ?= $(shell pkg-config --cflags x11 xext)
X11_LDFLAGS ?= $(shell pkg-config --libs x11 xext)

CFLAGS += -Wall -Wextra -pedantic -pedantic-errors -std=c99 -D_POSIX_C_SOURCE=200112 -D_GNU_SOURCE

OBJECTS = src/main.o src/daemon.o

all: $(BIN)

clangd: compile_flags.txt

compile_flags.txt:
	@echo $(CFLAGS) -I./includes $(X11_CFLAGS) > compile_flags.txt

%.o: %.c
	@echo CC $@
	@$(CC) $(CFLAGS) -I./includes $(X11_CFLAGS) -c -o $@ $<

$(BIN): $(OBJECTS)
	@echo LD $(BIN)
	@$(CC) $(CFLAGS) -I./includes $(X11_CFLAGS) -o $@ $< $(LDFLAGS) $(X11_LDFLAGS)

clean:
	@echo CLEAN
	@rm -rf compile_flags.txt $(OBJECTS) $(BIN)

.PHONY: clean all
