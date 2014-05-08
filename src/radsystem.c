/*---------------------------------------------------------------------------
 
  FILENAME:
        radsystem.c
 
  PURPOSE:
        Provide system initialization and process bookkeeping.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        02/06/2004      MST             0               Original
        02/08/2007      MST             1               Add 64-bit support
 
  NOTES:
 
 
  LICENSE:
        Copyright 2001-2005 Mark S. Teel. All rights reserved.

        Redistribution and use in source and binary forms, with or without 
        modification, are permitted provided that the following conditions 
        are met:

        1. Redistributions of source code must retain the above copyright 
           notice, this list of conditions and the following disclaimer.
        2. Redistributions in binary form must reproduce the above copyright 
           notice, this list of conditions and the following disclaimer in the 
           documentation and/or other materials provided with the distribution.

        THIS SOFTWARE IS PROVIDED BY Mark Teel ``AS IS'' AND ANY EXPRESS OR 
        IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
        WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
        DISCLAIMED. IN NO EVENT SHALL MARK TEEL OR CONTRIBUTORS BE LIABLE FOR 
        ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
        DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
        OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
        HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, 
        STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
        IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
        POSSIBILITY OF SUCH DAMAGE.
  
----------------------------------------------------------------------------*/

/*  ... System header files
*/

/*  ... Local header files
*/
#include <radsystem.h>

#define _RAD_DBG_ENABLED        0
#include <raddebug.h>




extern int radQueueSystemInit (int initFlag);
extern int radQueueSystemExit (int destroyFlag);



/*  ... local memory
*/
static SYSTEM_CB        systemWork;


/*  ... define IPC keys (and user area) here
*/
UINT    KEY_USER_START;
UINT    KEY_USER_STOP;
UINT    KEY_MSGQ;
UINT    KEY_SEMAPHORES;
UINT    KEY_BUFFERS_SHMEM;
UINT    KEY_CONFIG_SHMEM;


/*  ... local utilities
*/
/*  ... take (lock) a semaphore
*/
static int semInit (void)
{
    union semun arg;

    if ((systemWork.semId = semget (RADLIB_SYSTEM_SEM_KEY,
                                    1,
                                    IPC_CREAT | 0644))
            == -1)
    {
        systemWork.semId = 0;
        return ERROR;
    }

    arg.val = 1;
    if (semctl (systemWork.semId, 0, SETVAL, arg) == -1)
    {
        return ERROR;
    }

    return OK;
}

static void semTake (void)
{
    struct sembuf   smBuf = {0, -1, 0};

    semop (systemWork.semId, &smBuf, 1);

    return;
}


/*  ... give (unlock) a semaphore
*/
static void semGive (void)
{
    struct sembuf   smBuf = {0, 1, 0};

    semop (systemWork.semId, &smBuf, 1);

    return;
}

static void semDestroy (void)
{
    union semun     semCtl;

    semctl (systemWork.semId, 0, IPC_RMID, semCtl);

    return;
}


/*  ... radSystemInit: initialize a unique radlib system and/or register
    ...     the calling process within an existing radlib system;
    ... ALL processes within a radlib system must call this method
    ...     BEFORE calling radProcessInit;
    ... if this is the first process in the system to call this, system
    ...     facilities will be created and initialized, otherwise this
    ...     will just register the process with existing system facilities;
    ... care must be taken that all processes within a given radlib system
    ...     use the same unique systemID;
    ... returns OK or ERROR
*/
int radSystemInit (UCHAR systemID)
{
    return radSystemInitBuffers (systemID, NULL);
}


/*  ... radSystemInitBuffers: initialize a unique radlib system and/or
    ... register the calling process within an existing radlib system;
    ... ALL processes within a radlib system must call this method
    ...     BEFORE calling radProcessInit;
    ... if this is the first process in the system to call this, system
    ...     facilities will be created and initialized, otherwise this
    ...     will just register the process with existing system facilities;
    ... care must be taken that all processes within a given radlib system
    ...     use the same unique systemID;
    ... if "bufferCounts" is non-NULL, it will be used as the initialization
    ...     array for the system buffers, if NULL, the defaults will be
    ...     used (see radsysdefs.c for the default array and as an example,
    ...     and radsysdefs.h for the min, max and number of sizes 
    ...     definitions);
    ... returns OK or ERROR
*/
int radSystemInitBuffers (UCHAR systemID, int *bufferCounts)
{
    int         i;
    int         *bfrCounts;

    radDEBUGLog ("SystemInit: START");


    /*  ... first, initialize the system semaphore (just one),
        ... or attach to existing
    */
    if (semInit () == ERROR)
    {
        return ERROR;
    }

    semTake ();

    /*  ... does the shared memory already exist?
    */
    if ((systemWork.shmId = shmget (RADLIB_SYSTEM_SHM_KEY, 0, 0664)) != -1)
    {
        systemWork.share = (SYSTEM_SHARE *) shmat (systemWork.shmId, 0, 0);
        if (systemWork.share == (void *)-1)
        {
            return ERROR;
        }

        radDEBUGLog ("SystemInit: ATTACH %d", 
                     systemWork.share->numAttached + 1);
        systemWork.share->numAttached ++;
    }
    else
    {
        radDEBUGLog ("SystemInit: INIT");

        /*      ... we must get our shared memory first
        */
        if ((systemWork.shmId = shmget (RADLIB_SYSTEM_SHM_KEY,
                                        sizeof (SYSTEM_SHARE),
                                        IPC_CREAT | 0664))
                == -1)
        {
            return ERROR;
        }

        radDEBUGLog ("SystemInit: SHM");
        systemWork.share = (SYSTEM_SHARE *) shmat (systemWork.shmId, 0, 0);
        if (systemWork.share == (void *)-1)
        {
            return ERROR;
        }

        /*      ... initialize the shared memory
        */
        systemWork.share->numAttached = 1;
        for (i = 0; i < MAX_RADLIB_SYSTEMS; i ++)
        {
            systemWork.share->systems[i].numprocs = 0;
            systemWork.share->systems[i].keyBase = ((UINT)i << 16);
        }
    }


    //  ... now, set-up our process and system ID data

    // set the key values
    KEY_USER_START      = systemWork.share->systems[systemID].keyBase + 0x0001;
    KEY_USER_STOP       = systemWork.share->systems[systemID].keyBase + 0xF000;
    KEY_MSGQ            = systemWork.share->systems[systemID].keyBase + 0xF001;
    KEY_SEMAPHORES      = systemWork.share->systems[systemID].keyBase + 0xF002;
    KEY_BUFFERS_SHMEM   = systemWork.share->systems[systemID].keyBase + 0xF003;
    KEY_CONFIG_SHMEM    = systemWork.share->systems[systemID].keyBase + 0xF004;


    /*  ... are we the first here?
    */
    if (systemWork.share->systems[systemID].numprocs == 0)
    {
        radDEBUGLog ("SystemInit: SEMS");
        radSemSetDestroy ();

        if (radSemProcessInit () == ERROR)
        {
            radMsgLogInit (RADLIB_SYSTEM_NAME, TRUE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "radSemProcessInit failed: %d\n", errno);
            radMsgLogExit ();
            semGive ();
            return ERROR;
        }

        // was a custom buffer size array passed in?
        if (bufferCounts)
            bfrCounts = bufferCounts;
        else
            bfrCounts = sysBufferCounts;

        radDEBUGLog ("SystemInit: BUFFERS");
        if (radBuffersInit (SYS_BUFFER_SMALLEST_SIZE,
                            SYS_BUFFER_LARGEST_SIZE,
                            bfrCounts)
            == ERROR)
        {
            radMsgLogInit (RADLIB_SYSTEM_NAME, TRUE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "radBuffersInit failed!");
            radMsgLogExit ();
            radSemSetDestroy ();
            semGive ();
            return ERROR;
        }

        radDEBUGLog ("SystemInit: QUEUE");
        if (radQueueSystemInit (TRUE) == ERROR)
        {
            radMsgLogInit (RADLIB_SYSTEM_NAME, TRUE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "radQueueSystemInit failed!");
            radMsgLogExit ();
            radBuffersExitAndDestroy ();
            radSemSetDestroy ();
            semGive ();
            return ERROR;
        }

        systemWork.share->systems[systemID].startTime = radTimeGetSECSinceEpoch ();
        systemWork.share->systems[systemID].startTimeMS = radTimeGetMSSinceEpoch ();
    }
    else
    {
        radDEBUGLog ("SystemInit: SEMATTACH");
        if (radSemProcessInit () == ERROR)
        {
            radMsgLogInit (RADLIB_SYSTEM_NAME, TRUE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "radSemProcessInit failed!\n");
            radMsgLogExit ();
            semGive ();
            return ERROR;
        }

        radDEBUGLog ("SystemInit: BUFATTACH");
        if (radBuffersInit (0, 0, 0) == ERROR)
        {
            radMsgLogInit (RADLIB_SYSTEM_NAME, TRUE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "radBuffersInit failed!");
            radMsgLogExit ();
            semGive ();
            return ERROR;
        }

        radDEBUGLog ("SystemInit: QUEUEATTACH");
        if (radQueueSystemInit (FALSE) == ERROR)
        {
            radMsgLogInit (RADLIB_SYSTEM_NAME, TRUE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "radQueueSystemInit failed!");
            radMsgLogExit ();
            radBuffersExit ();
            semGive ();
            return ERROR;
        }
    }

    systemWork.share->systems[systemID].numprocs ++;
    semGive ();

    radDEBUGLog ("SystemInit: STOP");

    return OK;
}



/*  ... radSystemExit: deregister the calling process from a unique radlib
    ...     system and destroy all facilities if it is the last process
    ...     left in the radlib system;
    ... care must be taken that all processes within a given radlib system
    ...     use the same unique systemID
*/
void radSystemExit (UCHAR systemID)
{
    semTake ();

    /*  ... are we the last to exit this system ID?
    */
    systemWork.share->systems[systemID].numprocs --;
    if (systemWork.share->systems[systemID].numprocs == 0)
    {
        // destroy the queue table
        radQueueSystemExit (TRUE);

        // destroy buffers
        radBuffersExitAndDestroy ();

        // destroy the semaphore set
        radSemSetDestroy ();
    }
    else
    {
        // destroy the queue table
        radQueueSystemExit (FALSE);

        // detach from buffers
        radBuffersExit ();
    }


    /*  ... are we the last to be attached to the global system ID area?
    */
    systemWork.share->numAttached --;
    if (systemWork.share->numAttached == 0)
    {
        // destroy it!
        shmdt (systemWork.share);
        shmctl (systemWork.shmId, IPC_RMID, NULL);
        semDestroy ();
        return;
    }
    else
    {
        shmdt (systemWork.share);
        semGive ();
    }

    return;
}


/*  ... radSystemGetUpTimeMS: return the number of milliseconds since
    ...     radSystem was first called with this "systemID";
    ... care must be taken that all processes within a given radlib system
    ...     use the same unique systemID;
    ... returns number of milliseconds
    ... Note: milliseconds will roll over every ~49 days when stored in
    ...       a ULONG (as is done here)
*/
ULONG radSystemGetUpTimeMS (UCHAR systemID)
{
    return (ULONG)(radTimeGetMSSinceEpoch () -
            systemWork.share->systems[systemID].startTimeMS);
}


/*  ... radSystemGetUpTimeSEC: return the number of seconds since
    ...     radSystem was first called with this "systemID";
    ... care must be taken that all processes within a given radlib system
    ...     use the same unique systemID;
    ... returns number of seconds
*/
ULONG radSystemGetUpTimeSEC (UCHAR systemID)
{
    ULONG       retVal;

    retVal = radTimeGetSECSinceEpoch () -
             systemWork.share->systems[systemID].startTime;

    return retVal;
}


/*  ... radSystemGetUpTimeSTR: return a string of the form:
    ...     "Y years, M months, D days, H hours, m minutes, S seconds"
    ...     which represents the time since radSystem was first called with
    ...     this "systemID";
    ... care must be taken that all processes within a given radlib system
    ...     use the same unique systemID;
    ... returns a pointer to a static buffer containing the time string;
    ... Note: this call is not reentrant and successive calls will
    ...     overwrite the static buffer contents
*/
char *radSystemGetUpTimeSTR (UCHAR systemID)
{
    static char     upTimeBuffer[256];
    int             yr, mo, day, hour, min;
    ULONG           seconds;

    seconds = radTimeGetSECSinceEpoch () -
              systemWork.share->systems[systemID].startTime;

    yr = seconds / _SECONDS_IN_YEAR;
    seconds %= _SECONDS_IN_YEAR;
    mo = seconds / _SECONDS_IN_MONTH;
    seconds %= _SECONDS_IN_MONTH;
    day = seconds / _SECONDS_IN_DAY;
    seconds %= _SECONDS_IN_DAY;
    hour = seconds / _SECONDS_IN_HOUR;
    seconds %= _SECONDS_IN_HOUR;
    min = seconds / _SECONDS_IN_MINUTE;
    seconds %= _SECONDS_IN_MINUTE;

    sprintf (upTimeBuffer,
             "%d years, %d months, %d days, %d hours, %d minutes, %lu seconds",
             yr, mo, day, hour, min, seconds);

    return upTimeBuffer;
}

