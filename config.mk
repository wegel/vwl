_VERSION = 0.0.1-dev
VERSION  = `git describe --tags --always --dirty 2>/dev/null || echo $(_VERSION)`

PKG_CONFIG ?= pkg-config

PREFIX = /usr/local
MANDIR = $(PREFIX)/share/man
DATADIR = $(PREFIX)/share

WLR_PKG = wlroots-0.20
WLR_INCS = `$(PKG_CONFIG) --cflags $(WLR_PKG)`
WLR_LIBS = `$(PKG_CONFIG) --libs $(WLR_PKG)`

# Uncomment to enable XWayland
#XWAYLAND = -DXWAYLAND
#XLIBS = xcb xcb-icccm

CC = cc

# Local overrides are not tracked by git.
-include config.local.mk
