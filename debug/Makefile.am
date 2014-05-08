# Makefile - raddebug

#define the executable to be built
bin_PROGRAMS = raddebug

# define include directories
INCLUDES = \
		-I$(top_srcdir)/h \
		-D_GNU_SOURCE

# define the sources
raddebug_SOURCES  = \
		$(top_srcdir)/debug/raddebug.c

# define libraries
raddebug_LDADD   = -lrad -lpthread 

if MYSQL
raddebug_LDADD   += -lmysqlclient
else
if PGRESQL
raddebug_LDADD   += -lpq
endif
endif
if SQLITE
raddebug_LDADD   += -lsqlite3
endif

# define library directories
raddebug_LDFLAGS = -L../src/.libs -L$(prefix)/lib -L/usr/lib
INCLUDES         += -I$(prefix)/include -I/usr/include

if MYSQL
raddebug_LDFLAGS += -L$(prefix)/lib64/mysql -L$(prefix)/lib/mysql -L/usr/lib64/mysql -L/usr/lib/mysql
else
if PGRESQL
raddebug_LDFLAGS += -L$(prefix)/pgsql/lib
INCLUDES         += -I$(prefix)/pgsql/include
endif
endif

if CROSSCOMPILE
raddebug_LDFLAGS += $(prefix)/lib/crt1.o $(prefix)/lib/crti.o $(prefix)/lib/crtn.o
endif
