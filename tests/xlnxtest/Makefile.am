AM_CFLAGS = \
	-I$(top_srcdir)/include/drm \
	-I$(top_srcdir)/libkms/ \
	-I$(top_srcdir)

if HAVE_INSTALL_TESTS
bin_PROGRAMS = \
	xlnxtest
else
noinst_PROGRAMS = \
	xlnxtest
endif

xlnxtest_SOURCES = \
	buffers.c xlnxtest.c buffers.h xlnx_driver.c xlnx_driver.h

xlnxtest_LDADD = \
	$(top_builddir)/libdrm.la \
	$(top_builddir)/libkms/libkms.la

if HAVE_CAIRO
AM_CFLAGS += $(CAIRO_CFLAGS)
xlnxtest_LDADD += $(CAIRO_LIBS)
endif
