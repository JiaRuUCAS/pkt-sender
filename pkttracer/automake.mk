lib_LIBRARIES += libpkttracer.a

libpkttracer_a_CFLAGS = $(AM_CFLAGS)
#libpkttracer_a_LDFLAGS = $(AM_LDFLAGS) $(DPDK_LDFLAGS)
libpkttracer_a_CPPFLAGS = $(AM_CPPFLAGS)
libpkttracer_a_include_HEADERS = pkttracer/pt_trace.h
libpkttracer_a_includedir = $(includedir)/pkttracer
libpkttracer_a_SOURCES = pkttracer/pt_trace.c
