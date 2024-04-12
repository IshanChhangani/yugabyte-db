# contrib/yb_query_diagnostics/Makefile

MODULE_big = yb_query_diagnostics
OBJS = yb_query_diagnostics.o $(WIN32RES)

EXTENSION = yb_query_diagnostics
DATA =
PGFILEDESC = "yb_query_diagnostics - Yugabyte Query Diagnostics Extension"

REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/yb_query_diagnostics/yb_query_diagnostics.conf
REGRESS = yb_query_diagnostics

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/yb_query_diagnostics
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif