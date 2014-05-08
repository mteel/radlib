###############################################################################
#                                                                             #
#  Makefile for the database tests                                            #
#                                                                             #
#  Name                 Date           Description                            #
#  -------------------------------------------------------------------------  #
#  MS Teel              01/10/02       Initial Creation                       #
#                                                                             #
###############################################################################
#  Define the C compiler and its options
CC			= gcc
CC_OPTS			= -Wall -g -O2
SYS_DEFINES		= \
			-D_GNU_SOURCE \
			-D_LINUX

#  Define the Linker and its options
LD			= gcc
LD_OPTS			=

#  Define the Library creation utility and it's options
LIB_EXE			= ar
LIB_EXE_OPTS		= -rv

#  Define the dependancy generator
DEP			= gcc -MM

################################  R U L E S  ##################################
#  Generic rule for c files
%.o: %.c
	@echo "Building   $@"
	$(CC) $(CC_OPTS) $(SYS_DEFINES) $(DEFINES) $(INCLUDES) -c $< -o $@


#  Define some general usage vars
#  subsection of our build...
ifeq ($(DB), MYSQL)
DB_LIB_DIR		= /usr/lib/mysql
DB_INCLUDE_DIR		= /usr/include/mysql
else
DB_LIB_DIR		= /usr/lib -L/usr/local/lib
DB_INCLUDE_DIR		= /usr/include -I/usr/local/include
endif

#  Libraries
LIBS			= \
			-lc \
			-lz \
			-lrad

LIBS			+= -lsqlite3

LIBPATH 		= \
			-L$(DB_LIB_DIR)

#  Declare build defines
DEFINES			= \
			-D_DEBUG

#  Any build defines listed above should also be copied here
INCLUDES		= \
			-I. \
			-I$(DB_INCLUDE_DIR) 

########################### T A R G E T   I N F O  ############################
EXE_IMAGE		= dbtest

TEST_OBJS		= \
			./dbtest.o


#########################  E X P O R T E D   V A R S  #########################


################################  R U L E S  ##################################

$(EXE_IMAGE):	$(TEST_OBJS) 
	@echo "Linking $@..."
	@$(LD) $(LD_OPTS) $(LIBPATH) -o $@ \
	$(TEST_OBJS) \
	$(LIBS)

all: clean $(EXE_IMAGE)


#  Cleanup rules...
clean: 
	rm -rf \
	$(EXE_IMAGE) \
	$(TEST_OBJS)

