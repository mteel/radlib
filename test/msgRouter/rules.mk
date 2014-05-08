###############################################################################
#                                                                             #
#  General rules for Makefiles.                                               #
#                                                                             #
#  Name                 Date           	Description                           #
#  -------------------------------------------------------------------------  #
#  Yourname             01/01/04        Initial Creation                      #
#                                                                             #
###############################################################################

#  Define the C compiler and its options
CC_OPTS			= $(USER_CFLAGS)
SYS_DEFINES		= \
			-D_GNU_SOURCE \
			-D_LINUX

#  Define the Linker and its options
LD			= $(CC)
LD_OPTS			=

#  Define the Library creation utility and it's options
LIB_EXE			= $(AR)
LIB_EXE_OPTS		= -rvs

#  Define the dependancy generator
DEP			= $(CC) -M $(CC_OPTS) $(SYS_DEFINES) $(DEFINES) $(INCLUDES)



################################  R U L E S  ##################################
#  Generic rule for c files
%.o: %.c
	@echo "Building   $@"
	$(CC) $(CC_OPTS) $(SYS_DEFINES) $(DEFINES) $(INCLUDES) -c $< -o $@


# Generic rule for dependency files
%.d: %.c
	@$(DEP) $< -o $@
	@cat $@ | sed 's/$(*F).o/$(subst /,\/,$(@D)/$(*F).o)/' > $@
