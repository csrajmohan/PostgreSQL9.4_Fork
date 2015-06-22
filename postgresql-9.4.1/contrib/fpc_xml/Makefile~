MODULES = fpc_xml

ifdef USE_PGXS
PG_CONFIG = /home/dsladmin/quest/backend/pgsql/bin/pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/fpc_xml
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# fpc_xml.c includes core_postgres.c and make_join_rel.c
fpc_xml.o: core_postgres.c make_join_rel.c
