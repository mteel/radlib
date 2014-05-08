#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <radsysdefs.h>
#include <radbuffers.h>
#include <radsemaphores.h>
#include <radsystem.h>
#include <radprocess.h>
#include <radmsgRouter.h>

static void msgHandler
(
    char        *srcQueueName,
    UINT        msgType,
    void        *msg,
    UINT        length,
    void        *userData
)
{
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

static void USAGE (void)
{
    printf ("USAGE: raddebug [radlibSystemID] <msgRouterWorkDir>\n");
    printf ("           radlibSystemID    - (required) radlib system ID (1-255) to debug\n");
    printf ("           msgRouterWorkDir  - (optional) radlib msg router working directory\n");
    return;
}

int main (int argc, char *argv[])
{
    int         sysID;
    char        qname[128], refname[128];

    if (argc < 2)
    {
        USAGE ();
        return 1;
    }
    sysID = atoi (argv[1]);

    if (sysID < 1 || sysID > 255)
    {
        printf ("Invalid system ID!\n");
        USAGE ();
        return 1;
    }

    if (radSystemInit ((UCHAR)sysID) == ERROR)
    {
        printf ("\nError: unable to attach to wview radlib system %d!\n",
                sysID);
        return 1;
    }

    printf ("\nAttached to radlib system %d: UP %s\n\n",
            sysID, radSystemGetUpTimeSTR (sysID));

    // dump out the system buffer info
    radBuffersDebug ();
    printf ("\n");

    // dump out semaphore info
    radSemDebug ();
    printf ("\n");

    // if the message router work directory was given, try to dump his stats
    if (argc > 2)
    {
        //  call the radlib process init function
        sprintf (qname, "%s/raddebugFIFO", argv[2]);
        sprintf (refname, "%s/raddebugFIFOREF", argv[2]);
        if (radProcessInit ("raddebug",
                            qname,
                            0,
                            FALSE,                     // FALSE => not as daemon
                            msgHandler,
                            evtHandler,
                            NULL)
            == ERROR)
        {
            printf ("radProcessInit failed\n");
            radSystemExit ((UCHAR)sysID);
            return 1;
        }

        if (radMsgRouterInit (argv[2]) == ERROR)
        {
            printf ("Invalid msg router work directory %s given or router not running!\n", 
                    argv[2]);
        }
        else
        {
            printf ("Dumping message router stats to the system log file...\n\n");
            radMsgRouterStatsDump ();
            radMsgRouterExit ();
        }

        radProcessExit ();
        unlink (qname);
        unlink (refname);
    }

    radSystemExit ((UCHAR)sysID);
    return 0;
}

