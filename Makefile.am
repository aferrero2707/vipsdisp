SUBDIRS = bp

bin_PROGRAMS = vipsdisp resize

vipsdisp_SOURCES = \
	disp-gresources.c \
	disp.c \
	disp.h \
	imageview.c \
	imageview.h \
	imagepresent.c \
	imagepresent.h \
	imagedisplay.c \
	imagedisplay.h \
	icc_transform_float.c \
	icc_transform_float.h

resize_SOURCES = \
	resize.c \
	disp.h \
	imageview.c \
	imageview.h \
	imagepresent.c \
	imagepresent.h \
	imagedisplay.c \
	imagedisplay.h \
	icc_transform_float.c \
	icc_transform_float.h

AM_CPPFLAGS = @PKGFLAGS_CFLAGS@
LDADD = @PKGFLAGS_CFLAGS@ @PKGFLAGS_LIBS@
AM_LDFLAGS = @LDFLAGS@

resource_files = \
	disp.gresources.xml \
	gtk/imageview-popover.ui \
	gtk/menus.ui

disp-gresources.c: $(resource_files)
	glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source $<

CLEANFILES = disp-gresources.c

EXTRA_DIST = $(resource_files)


