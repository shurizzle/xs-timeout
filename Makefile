BIN = xs-timeout
VERSION = 0.0.1

X11_CFLAGS ?= $(shell pkg-config --cflags x11 xext)
X11_LDFLAGS ?= $(shell pkg-config --libs x11 xext)

CFLAGS += -Wall -Wextra -pedantic -pedantic-errors -std=c99 -D_POSIX_C_SOURCE=200112 -D_GNU_SOURCE

OBJECTS = src/main.o src/daemon.o src/timeouts.o src/options.o src/idle.o

all: $(BIN)

clangd: compile_flags.txt

compile_flags.txt:
	@(for flag in $(CFLAGS) -I./includes $(X11_CFLAGS); do echo "$$flag"; done) > compile_flags.txt

%.o: %.c
	@echo CC $@
	@$(CC) $(CFLAGS) -I./includes $(X11_CFLAGS) -c -o $@ $<

$(BIN): $(OBJECTS)
	@echo LD $(BIN)
	@$(CC) $(CFLAGS) -I./includes $(X11_CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LDFLAGS)

valgrind: $(BIN)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes -s $(BIN)

CLEAN_FILES := $(OBJECTS) $(BIN)

ifneq (,$(shell which dpkg 2>/dev/null))

DEB_ARCH := $(shell dpkg --print-architecture)
DEB_VERSION ?= 1

DEBDIR = $(BIN)_$(VERSION)-$(DEB_VERSION)_$(DEB_ARCH)
DEB = $(DEBDIR).deb

CLEAN_FILES += $(DEBDIR) $(DEB)

#libx11-6 libxext6

deb: $(DEB)

$(DEBDIR)/DEBIAN/control:
	@mkdir -p "$(DEBDIR)/DEBIAN"
	@(echo "Package: $(BIN)"; \
		echo "Version: $(VERSION)"; \
		echo "Architecture: $(DEB_ARCH)"; \
		echo "Maintainer: shurizzle <me@shurizzle.dev>"; \
		echo "Description:  Executes commands on user idle."; \
		echo "Depends: libx11-6 (>= 2:1.6.0), libxext6 (>= 2:1.3.0)" \
	) > "$(DEBDIR)/DEBIAN/control"

$(DEBDIR)/usr/bin/$(BIN): $(BIN)
	@mkdir -p "$(DEBDIR)/usr/bin"
	@cp -af "$(BIN)" "$(DEBDIR)/usr/bin/$(BIN)"

$(DEB): $(DEBDIR)/DEBIAN/control $(DEBDIR)/usr/bin/$(BIN)
	@dpkg-deb --build --root-owner-group "$(DEBDIR)"

.PHONY: deb
endif

clean:
	@echo CLEAN
	@rm -rf $(CLEAN_FILES)

deep_clean: clean
	@rm -rf compile_flags.txt compile_commands.json

.PHONY: clean deep_clean all clangd valgrind
