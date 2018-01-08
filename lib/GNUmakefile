
# There to install files built in this directory
INSTALL_DIR = $(PREFIX)/lib

# The libcrts_radio.so is not for CRTS users.  It's so that the program
# crts_radio may override bad behavior in libuhd.so.  See file README and
# libcrts_radio.c.
libcrts_radio.so_SOURCES := libcrts_radio.c debug.c Filter.cpp
libcrts_radio.so_CPPFLAGS := -DSPEW_FILE=stderr -I ../include


include ../quickbuild.make
