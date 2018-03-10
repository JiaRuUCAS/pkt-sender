bin_PROGRAMS += pktsender

#dpdk_ipsec_LDADD = $(top_srcdir)/proto/libipsec_proto.a
pktsender_LDFLAGS = $(AM_LDFLAGS) $(DPDK_LDFLAGS)
pktsender_CFLAGS = $(AM_CFLAGS)
pktsender_CPPFLAGS = $(AM_CPPFLAGS) -I pkttracer/
pktsender_SOURCES = src/main.c \
					src/pktsender.c \
					src/pkt_seq.c \
					src/port.c \
					src/probe.c \
					src/stat.c \
					src/transmitter.c
pktsender_LDADD = libpkttracer.a
