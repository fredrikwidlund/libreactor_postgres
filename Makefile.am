ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
AM_CFLAGS = -std=gnu11 -g -O3 -flto -fuse-linker-plugin -I$(srcdir)/src
AM_LDFLAGS = -static -lpq

EXTRA_DIST = \
CHANGES \
LICENSE \
README.rst

SOURCE_FILES = \
src/reactor_postgres/reactor_postgres.c \
src/reactor_postgres/reactor_postgres_client.c

HEADER_FILES = \
src/reactor_postgres/reactor_postgres.h \
src/reactor_postgres/reactor_postgres_client.h

AUTOMAKE_OPTIONS = subdir-objects
lib_LTLIBRARIES = libreactor_postgres.la
libreactor_postgres_la_SOURCES = $(SOURCE_FILES) $(HEADER_FILES)

headerfilesdir = $(includedir)/reactor_postgres
headerfiles_HEADERS = $(HEADER_FILES)

mainheaderdir = $(includedir)
mainheader_HEADERS = src/reactor_postgres.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libreactor_postgres.pc

## examples ###

bin_PROGRAMS = \
example/query_lowlevel \
example/queries

example_query_lowlevel_LDFLAGS = -lreactor_postgres -lreactor -ldynamic -lclo
example_queries_LDFLAGS = -lreactor_postgres -lreactor -ldynamic -lclo

MAINTAINERCLEANFILES = aclocal.m4 config.h.in configure Makefile.in docs/Makefile.in libreactor_postgres-?.?.?.tar.gz
maintainer-clean-local:; rm -rf autotools m4 libreactor_postgres-?.?.?

CLEANFILES = {.,src/reactor_postgres}/*.{gcno,gcda,gcov}
