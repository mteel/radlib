# Makefile - radmrouted

#define the executable to be built
bin_PROGRAMS = radmrouted

# define include directories
INCLUDES = \
		-I$(top_srcdir)/h \
		-D_GNU_SOURCE

# define the sources
radmrouted_SOURCES  = \
		$(top_srcdir)/msgRouter/msgRouter.c

# define libraries
radmrouted_LDADD   = -lrad -lpthread

if MYSQL
radmrouted_LDADD   += -lmysqlclient
else
if PGRESQL
radmrouted_LDADD   += -lpq
endif
endif
if SQLITE
radmrouted_LDADD   += -lsqlite3
endif

# define library directories
radmrouted_LDFLAGS = -L../src/.libs -L$(prefix)/lib -L/usr/lib
INCLUDES           += -I$(prefix)/include -I/usr/include

if MYSQL
radmrouted_LDFLAGS += -L$(prefix)/lib64/mysql -L$(prefix)/lib/mysql -L/usr/lib64/mysql -L/usr/lib/mysql
else
if PGRESQL
radmrouted_LDFLAGS += -L$(prefix)/lib -L$(prefix)/pgsql/lib
INCLUDES           += -I$(prefix)/pgsql/include
endif
endif

if CROSSCOMPILE
radmrouted_LDFLAGS += $(prefix)/lib/crt1.o $(prefix)/lib/crti.o $(prefix)/lib/crtn.o
endif
