#ifndef INC_routetesth
#define INC_routetesth
/*---------------------------------------------------------------------------
 
  FILENAME:
        routetest.h
 
  PURPOSE:
        Provide the radlib routetest process definitions.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        02/06/2010      MS Teel         0               Original
 
  NOTES:
 
----------------------------------------------------------------------------*/

// System include files
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>

// radlib include files
#include <radsysdefs.h>
#include <radsemaphores.h>
#include <radbuffers.h>
#include <radqueue.h>
#include <radtimers.h>
#include <radevents.h>
#include <radtimeUtils.h>
#include <radprocess.h>
#include <radstates.h>
#include <radconffile.h>
#include <radmsgRouter.h>

// Local include files


// process definitions

#define PROC_NAME_ROUTETEST         "routetestd"
#define ROUTETEST_LOCK_FILENAME     "routetest"
#define PROC_NUM_TIMERS_ROUTETEST   2

#define ROUTETEST_TIMER1_PERIOD     15000           // 15 seconds



// the "routetest" process work area
typedef struct
{
    pid_t           myPid;
    int             myRadSysID;
    char            myID[128];
    TIMER_ID        timerNum1;
    int             exiting;
} ROUTETEST_WORK;


// define the routetest message IDs
enum
{
    ROUTETEST_MSGID_USER_REQUEST     = 100,
    ROUTETEST_MSGID_USER_RESPONSE    = 101
};

// define the USER_REQUEST message
typedef struct
{
    char            id[128];
} ROUTETEST_MSG_USER_REQUEST;

// define the USER_RESPONSE message
typedef struct
{
    char            id[128];
    char            rqstid[128];
} ROUTETEST_MSG_USER_RESPONSE;



// function prototypes



#endif

