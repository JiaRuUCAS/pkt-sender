bin_PROGRAMS += pt_analyzer

#dpdk_ipsec_LDADD = $(top_srcdir)/proto/libipsec_proto.a
pt_analyzer_LDFLAGS = $(AM_LDFLAGS)
pt_analyzer_CFLAGS = $(AM_CFLAGS)
pt_analyzer_CPPFLAGS = $(AM_CPPFLAGS) -I pkttracer/ -I src/
pt_analyzer_SOURCES = tools/cmd_dump.c \
					  tools/command.c \
					  tools/cuckoohash.c \
					  tools/hash.c \
					  tools/pt_analyzer.c \
					  tools/ringbuffer.c
pt_analyzer_LDADD = libpkttracer.a
