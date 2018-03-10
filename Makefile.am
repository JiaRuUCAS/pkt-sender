AUTOMAKE_OPTIONS = foreign subdir-objects
#SUBDIRS = \
#		  ipsec

AM_CFLAGS = -Wstrict-prototypes
AM_CFLAGS += -Werror
AM_CFLAGS += -O2 -g -Wall
AM_CFLAGS += $(PS_CFLAGS)
#AM_CFLAGS += -D_FILE_OFFSET_BITS=64

#PS_CFLAGS += -Wall -Wstrict_prototypes

AM_LDFLAGS = $(PS_LDFLAGS)

if PS_DEBUG
AM_CPPFLAGS = -DPS_DEBUG
else
AM_CPPFLAGS =
AM_CFLAGS += -fomit-frame-pointer
endif


bin_PROGRAMS =
lib_LIBRARIES =

include pkttracer/automake.mk
include src/automake.mk
include tools/automake.mk
