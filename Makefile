.POSIX:
.SUFFIXES:

include config.mk

# flags for compiling
DWLCPPFLAGS = -I. -DWLR_USE_UNSTABLE -D_POSIX_C_SOURCE=200809L \
	-DVERSION=\"$(VERSION)\" $(XWAYLAND)
DWLDEVCFLAGS = -g -Wpedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-unused-parameter -Wshadow -Wunused-macros -Werror=strict-prototypes \
	-Werror=implicit -Werror=return-type -Werror=incompatible-pointer-types \
	-Wfloat-conversion

# CFLAGS / LDFLAGS
PKGS      = wayland-server xkbcommon libinput $(XLIBS)
DWLCFLAGS = `$(PKG_CONFIG) --cflags $(PKGS)` $(WLR_INCS) $(DWLCPPFLAGS) $(DWLDEVCFLAGS) $(CFLAGS)
LDLIBS    = `$(PKG_CONFIG) --libs $(PKGS)` $(WLR_LIBS) -lm $(LIBS)

all: vwl

vwl: vwl.o plumbing.o util.o vwl-ipc-unstable-v1-protocol.o
	$(CC) vwl.o plumbing.o util.o vwl-ipc-unstable-v1-protocol.o $(DWLCFLAGS) $(LDFLAGS) $(LDLIBS) -o $@
vwl.o: vwl.c vwl.h client.h config.h config.mk cursor-shape-v1-protocol.h \
	pointer-constraints-unstable-v1-protocol.h wlr-layer-shell-unstable-v1-protocol.h \
	wlr-output-power-management-unstable-v1-protocol.h xdg-shell-protocol.h \
	vwl-ipc-unstable-v1-protocol.h
plumbing.o: plumbing.c vwl.h util.h config.h
util.o: util.c util.h
vwl-ipc-unstable-v1-protocol.o: vwl-ipc-unstable-v1-protocol.c vwl-ipc-unstable-v1-protocol.h

# wayland-scanner is a tool which generates C headers and rigging for Wayland
# protocols, which are specified in XML. wlroots requires you to rig these up
# to your build system yourself and provide them in the include path.
WAYLAND_SCANNER   = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`

cursor-shape-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/staging/cursor-shape/cursor-shape-v1.xml $@
pointer-constraints-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		$(WAYLAND_PROTOCOLS)/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml $@
wlr-layer-shell-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) enum-header \
		protocols/wlr-layer-shell-unstable-v1.xml $@
wlr-output-power-management-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/wlr-output-power-management-unstable-v1.xml $@
xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		$(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
vwl-ipc-unstable-v1-protocol.h:
	$(WAYLAND_SCANNER) server-header \
		protocols/vwl-ipc-unstable-v1.xml $@
vwl-ipc-unstable-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code \
		protocols/vwl-ipc-unstable-v1.xml $@

config.h:
	cp config.def.h $@
clean:
	rm -f vwl *.o *-protocol.h *-protocol.c

dist: clean
	mkdir -p vwl-$(VERSION)
	cp -R LICENSE* Makefile CHANGELOG.md README.md client.h config.def.h \
		config.mk protocols dwl.1 vwl.c util.c util.h vwl.desktop VWL_FEATURES.md \
		vwl-$(VERSION)
	tar -caf vwl-$(VERSION).tar.gz vwl-$(VERSION)
	rm -rf vwl-$(VERSION)

install: vwl
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/vwl
	cp -f vwl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/vwl
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f dwl.1 $(DESTDIR)$(MANDIR)/man1
	chmod 644 $(DESTDIR)$(MANDIR)/man1/dwl.1
	mkdir -p $(DESTDIR)$(DATADIR)/wayland-sessions
	cp -f vwl.desktop $(DESTDIR)$(DATADIR)/wayland-sessions/vwl.desktop
	chmod 644 $(DESTDIR)$(DATADIR)/wayland-sessions/vwl.desktop
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/vwl $(DESTDIR)$(MANDIR)/man1/dwl.1 \
		$(DESTDIR)$(DATADIR)/wayland-sessions/vwl.desktop

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(DWLCFLAGS) -o $@ -c $<
