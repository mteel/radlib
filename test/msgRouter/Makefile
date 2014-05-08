###############################################################################
#                                                                             #
#  Makefile for the radlib router test process                                #
#                                                                             #
#  Name                 Date           Description                            #
#  -------------------------------------------------------------------------  #
#  Yourname             01/01/2004     Initial Creation                       #
#                                                                             #
###############################################################################

#  Define some general usage vars


#  Libraries
LIBPATH			= \
			-L/usr/local/lib

LIBS			= \
			-lc \
			-lm \
			-lrad

DB_LIBS			= \
			-lc \
			-lz \
			-lrad

DB_LIBPATH 		= 

#  Declare build defines
DEFINES			= \
			-D_DEBUG

#  Any build defines listed above should also be copied here
INCLUDES		= \
			-I/usr/local/include \
			-I.

########################### T A R G E T   I N F O  ############################
EXE_IMAGE		= ./routetest

ROUTETEST_OBJS		= \
			./routetest.o

#########################  E X P O R T E D   V A R S  #########################

#  Include the rules file for all of the common stuff
include ./rules.mk

################################  R U L E S  ##################################

all: $(EXE_IMAGE)

$(EXE_IMAGE):   $(ROUTETEST_OBJS)
	@echo "Linking $@..."
	@$(LD) $(LD_OPTS) $(LIBPATH) -o $@ \
	$(ROUTETEST_OBJS) \
	$(LIBS)


#  Cleanup rules...
clean:
	rm -rf \
	$(EXE_IMAGE) \
	$(ROUTETEST_OBJS) \

