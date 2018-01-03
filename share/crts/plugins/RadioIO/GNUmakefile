# This makefile can be used to build any of the modules
# that are at this directory level, share/crts/plugins/.

ROOT := ../../../../

CPPFLAGS := -I$(ROOT)include -I$(ROOT)lib -I$(ROOT)liquid-dsp/src

INSTALL_DIR = $(PREFIX)/share/crts/plugins/$(notdir $(CURDIR))


# Make every *.cpp file into a DSO (dynamic shared object) plugin that is
# installed.

# TODO: This will not work in a build tree that is not the same as the
# source tree.  *.cpp is in the source.  That may explain why GNU automake
# never uses wildcards.
srcs := $(patsubst %.cpp,%,$(wildcard *.cpp))

define MkSource
  $(1).so_SOURCES := $(1).cpp
endef
$(foreach src,$(srcs),$(eval $(call MkSource,$(src))))
undefine MkSource
undefine s

readline.so_LDFLAGS := -lreadline

#tun.so_SOURCES +=\
 $(ROOT)lib/getTunViaUSocket.cpp\
 $(ROOT)lib/debug.c
tun.so_CPPFLAGS := -DSPEW_FILE=stderr -DDEBUG



# above we auto generate lines like this:
#*.so_SOURCES := *.cpp

include $(ROOT)quickbuild.make
