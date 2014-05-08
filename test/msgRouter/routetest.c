/*---------------------------------------------------------------------------
 
  FILENAME:
        routetest.c
 
  PURPOSE:
        Provide the radlib msgRouter test process entry point.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        02/07/2010      MS Teel         0               Original
 
  NOTES:
 
----------------------------------------------------------------------------*/

// System include files
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// radlib include files

// Local include files
#include "routetest.h"


// global memory declarations

// global memory referenced

// static (local) memory declarations

// declare the process work area
static ROUTETEST_WORK    routetestWork;

// define the configuration file IDs in use
enum ConfigIds
{
    CFG_ID_FILE_DEV         = 0,
    CFG_ID_VERBOSE_MODE     = 1
};

// and the corresponding string identifiers
static char             *configIDs[] =
{
    "FILE_DEV",
    "VERBOSE_MSGS"
};


// methods

static char* GetHostname(void)
{
    FILE        *hfile;
    static char _hostname[64];

    hfile = fopen ("/etc/hostname", "r");
    if (hfile == NULL)
    {
        printf("ERROR: failed to OPEN /etc/hostname!");
        return "";
    }

    if (fscanf (hfile, "%s", _hostname) != 1)
    {
        printf("ERROR: failed to READ /etc/hostname!");
        fclose (hfile);
        return "";
    }

    fclose (hfile);
    return _hostname;
}

static void SendRequest(void)
{
    ROUTETEST_MSG_USER_REQUEST  msg;

    strncpy(msg.id, routetestWork.myID, sizeof(msg.id));
    radMsgRouterMessageSend(ROUTETEST_MSGID_USER_REQUEST, &msg, sizeof(msg));
}

static void SendResponse(char* rqstid)
{
    ROUTETEST_MSG_USER_RESPONSE msg;

    strncpy(msg.id, routetestWork.myID, sizeof(msg.id));
    strncpy(msg.rqstid, rqstid, sizeof(msg.rqstid));
    radMsgRouterMessageSend(ROUTETEST_MSGID_USER_RESPONSE, &msg, sizeof(msg));
}

static void msgHandler
(
    char        *srcQueueName,
    UINT        msgType,
    void        *msg,
    UINT        length,
    void        *userData
)
{
    switch(msgType)
    {
        case ROUTETEST_MSGID_USER_REQUEST:
        {
            ROUTETEST_MSG_USER_REQUEST* inmsg = (ROUTETEST_MSG_USER_REQUEST*)msg;
            if (!strncmp(inmsg->id, routetestWork.myID, sizeof(inmsg->id)))
            {
                break;
            }
            printf("RQST: %s ==> ALL\n", inmsg->id);
            SendResponse(inmsg->id);
            break;
        }
        case ROUTETEST_MSGID_USER_RESPONSE:
        {
            ROUTETEST_MSG_USER_RESPONSE* inmsg = (ROUTETEST_MSG_USER_RESPONSE*)msg;
            if (!strncmp(inmsg->id, routetestWork.myID, sizeof(inmsg->id)))
            {
                break;
            }
            printf("RESP: %s <== %s\n", inmsg->rqstid, inmsg->id);
            break;
        }
    }
    
    return;
}

static void evtHandler
(
    UINT        eventsRx,
    UINT        rxData,
    void        *userData
)
{
    return;
}

static void timer1Handler (void *parm)
{
    // Send RQST:
    SendRequest();
    
    radTimerStart (routetestWork.timerNum1, ROUTETEST_TIMER1_PERIOD);
    return;
}


// process initialization
static int routetestSysInit (int radID)
{
    char            devPath[256], temp[32];
    char            *installPath;
    struct stat     fileData;
    FILE            *pidfile;

    // get the install path
    installPath = getenv ("APPLICATION_RUN_DIRECTORY");
    if (installPath == NULL)
    {
        installPath = ".";
    }
    chdir (installPath);


    // create our device directory if it is not there
    sprintf (devPath, "%s/%d", installPath, radID);
    if (stat (devPath, &fileData) != 0)
    {
        if (mkdir (devPath, 0755) != 0)
        {
            printf ("Cannot create device dir: %s - aborting!\n",
                    devPath);
            return -1;
        }
    }

    return 0;
}

// system exit
static int routetestSysExit (int radID)
{
    char            devPath[256];
    char            *installPath;
    struct stat     fileData;

    // get the install path
    installPath = getenv ("APPLICATION_RUN_DIRECTORY");
    if (installPath == NULL)
    {
        installPath = ".";
    }

    // delete our pid file
    sprintf (devPath, "%s/%s%d.pid", installPath, ROUTETEST_LOCK_FILENAME, radID);
    if (stat (devPath, &fileData) == 0)
    {
        printf ("\nlock file %s exists, deleting it...\n",
                devPath);
        if (unlink (devPath) == -1)
        {
            printf ("lock file %s delete failed!\n",
                    devPath);
        }
    }

    return 0;
}

static void defaultSigHandler (int signum)
{
    switch (signum)
    {
        case SIGPIPE:
            // if you are using sockets or pipes, you will need to catch this
            // we have a far end socket disconnection, we'll handle it in the
            // "read/write" code
            signal (signum, defaultSigHandler);
            break;

        case SIGILL:
        case SIGBUS:
        case SIGFPE:
        case SIGSEGV:
        case SIGXFSZ:
        case SIGSYS:
            // unrecoverable signal - we must exit right now!
            radMsgLog(PRI_CATASTROPHIC, "routetestd: recv sig %d: bailing out!", signum);
            abort ();
        
        case SIGCHLD:
            wait (NULL);
            signal (signum, defaultSigHandler);
            break;

        default:
            // can we allow the process to exit normally?
            if (radProcessGetExitFlag())
            {
                // NO! - we gotta bail here!
                radMsgLog(PRI_HIGH, "routetestd: recv sig %d: exiting now!", signum);
                exit (0);                
            }
            
            // we can allow the process to exit normally...
            radMsgLog(PRI_HIGH, "routetestd: recv sig %d: exiting!", signum);
        
            radProcessSetExitFlag ();
        
            signal (signum, defaultSigHandler);
            break;
    }

    return;
}


// the main entry point for the routetest process
int main (int argc, char *argv[])
{
    void            (*alarmHandler)(int);
    STIM            stim;
    int             i, radSysID;
    char            qname[256];
    char            pidName[256];
    char            workdir[128];
    char            *installPath;
    FILE            *pidfile;

    if (argc < 2)
    {
        printf("You must specify the radlib system ID!\n");
        exit(1);
    }
    radSysID = atoi(argv[1]);

    // initialize some system stuff first
    if (routetestSysInit(radSysID) == -1)
    {
        radMsgLogInit (PROC_NAME_ROUTETEST, TRUE, TRUE);
        radMsgLog(PRI_CATASTROPHIC, "system init failed!\n");
        radMsgLogExit ();
        exit (1);
    }

    // create some file paths for later use
    installPath = getenv ("APPLICATION_RUN_DIRECTORY");
    if (installPath == NULL)
    {
        installPath = ".";
    }
    sprintf (qname, "%s/%d/%s", installPath, radSysID, PROC_NAME_ROUTETEST);
    sprintf (pidName, "%s/%s%d.pid", installPath, ROUTETEST_LOCK_FILENAME, radSysID);

    memset (&routetestWork, 0, sizeof (routetestWork));
    routetestWork.myRadSysID = radSysID;


    // call the global radlib system init function
    if (radSystemInit ((UCHAR)radSysID) == ERROR)
    {
        radMsgLogInit (PROC_NAME_ROUTETEST, TRUE, TRUE);
        radMsgLog(PRI_CATASTROPHIC, "%s: radSystemInit failed!");
        radMsgLogExit ();
        exit (1);
    }


    // call the radlib process init function
    if (radProcessInit (PROC_NAME_ROUTETEST,
                        qname,
                        PROC_NUM_TIMERS_ROUTETEST,
                        FALSE,                     // FALSE => not as daemon
                        msgHandler,
                        evtHandler,
                        NULL)
        == ERROR)
    {
        radMsgLogInit (PROC_NAME_ROUTETEST, TRUE, TRUE);
        radMsgLog(PRI_CATASTROPHIC, "radProcessInit failed: %s",
                   PROC_NAME_ROUTETEST);
        radMsgLogExit ();

        radSystemExit ((UCHAR)radSysID);
        exit (1);
    }

    // save our process pid and create the lock file 
    routetestWork.myPid = getpid ();
    pidfile = fopen (pidName, "w");
    if (pidfile == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "lock file create failed!\n");

        radProcessExit ();
        radSystemExit ((UCHAR)radSysID);
        exit (1);
    }
    fprintf (pidfile, "%d", routetestWork.myPid);
    fclose (pidfile);


    // save the current alarm signal handler, set all signal handlers
    // to the default handler, then set the alarm handler back to original
    alarmHandler = radProcessSignalGetHandler (SIGALRM);
    radProcessSignalCatchAll (defaultSigHandler);
    radProcessSignalCatch (SIGALRM, alarmHandler);

    sprintf(routetestWork.myID, "%s:%d:%d", GetHostname(), getpid(), radSysID);

    // create a timer:
    routetestWork.timerNum1 = radTimerCreate (NULL, timer1Handler, NULL);
    if (routetestWork.timerNum1 == NULL)
    {
        radMsgLog(PRI_HIGH, "radTimerCreate failed");

        radProcessExit ();
        radSystemExit ((UCHAR)radSysID);
        exit (1);
    }
    radTimerStart (routetestWork.timerNum1, ROUTETEST_TIMER1_PERIOD);


    // initialize the radlib message router interface
    sprintf(workdir, "./%d", radSysID);
    if (radMsgRouterInit (workdir) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterInit failed");
        radTimerDelete (routetestWork.timerNum1);

        radProcessExit ();
        radSystemExit ((UCHAR)radSysID);
        exit (1);
    }

    radMsgRouterMessageRegister(ROUTETEST_MSGID_USER_REQUEST);
    radMsgRouterMessageRegister(ROUTETEST_MSGID_USER_RESPONSE);


    printf ("\n%s: running...\n", routetestWork.myID);


    while (!routetestWork.exiting)
    {
        // wait on timers, events, file descriptors, msgs, everything!
        if (radProcessWait (0) == ERROR)
        {
            routetestWork.exiting = TRUE;
        }
    }


    radMsgLog(PRI_STATUS, "exiting normally...");

    radTimerDelete (routetestWork.timerNum1);
    routetestSysExit (radSysID);

    radProcessExit ();
    radSystemExit ((UCHAR)radSysID);
    exit (0);
}

