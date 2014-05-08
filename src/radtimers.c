/*---------------------------------------------------------------------------

  FILENAME:
        radtimers.c

  PURPOSE:
        Timer subsystem.

  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        5/26/95         M.S. Teel       0               original
        11/20/96        M.S. Teel       1               port to pSos ARM
        9/28/99         MS Teel         2               PORT TO Psos 2.50
        12/25/01        MS Teel         3               Port to linux
        04/04/2008      M.S. Teel       4               Rewrite using radlist

  NOTES:


  LICENSE:
        Copyright 2001-2008 Mark S. Teel. All rights reserved.

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

/*  ... OS include files
*/

/*  ... System include files
*/

/*  ... Local include files
*/
#include <radtimers.h>


/*  ... global memory declarations
*/

/*  ... static (local) memory declarations
*/
static TIMER_LIST_ID    timerList;


//  ... subsystem internal calls

//  ... decrement all timers; return shortest delta on list
static ULONG updateTimers (ULONG delta)
{
    register TIMER_ID   timer;
    ULONG               retVal = 0xFFFFFFFF;

    for (timer = (TIMER_ID) radListGetFirst (&timerList->pendingList);
         timer != NULL;
         timer = (TIMER_ID) radListGetNext (&timerList->pendingList, (NODE_PTR)timer))
    {
        if (timer->deltaTime <= delta)
        {
            timer->deltaTime = 0;
        }
        else
        {
            timer->deltaTime -= delta;
        }

        if (timer->deltaTime < retVal)
        {
            retVal = timer->deltaTime;
        }
    }

    return retVal;
}

//  ... process expired timers
static void processExpiredTimers (void)
{
    register TIMER_ID   timer, prevTimer;
    SYS_CALLBACK_MSG    cbMsg;
    int                 retVal;

    for (timer = (TIMER_ID) radListGetFirst (&timerList->pendingList);
         timer != NULL;
         timer = (TIMER_ID) radListGetNext (&timerList->pendingList, (NODE_PTR)timer))
    {
        if (timer->deltaTime == 0)
        {
            // remove from pending list
            prevTimer = (TIMER_ID) radListGetPrevious (&timerList->pendingList, (NODE_PTR)timer);
            radListRemove (&timerList->pendingList, (NODE_PTR)timer);

            timer->pending = FALSE;

            // send expiry notification
            if (timer->routine != NULL)
            {
                cbMsg.length    = sizeof (cbMsg) - sizeof (cbMsg.length);
                cbMsg.msgType   = 0;
                cbMsg.callback  = timer->routine;
                cbMsg.parm      = timer->parm;
                retVal = write (timerList->notifyFD, &cbMsg, sizeof (cbMsg));
                if (retVal != sizeof (cbMsg))
                {
                    radMsgLog(PRI_HIGH, "processExpiredTimers: write to notify fd failed: %s",
                               strerror (errno));
                }
            }

            timer = prevTimer;
        }
    }

    return;
}

//  ... Tick off delta time and run timeout routines as needed
static int serviceTimers (int ifProcessExpiredTimers)
{
    ULONG   numberOfTicks, smallestDelta;

    // is this the first time?
    if (timerList->lastTick == 0ULL)
    {
        timerList->lastTick = radTimeGetMSSinceEpoch ();
    }

    numberOfTicks = (ULONG)(radTimeGetMSSinceEpoch () - timerList->lastTick);
    timerList->lastTick = radTimeGetMSSinceEpoch ();

    // update timers with number of expired ticks
    smallestDelta = updateTimers (numberOfTicks);
    if (ifProcessExpiredTimers && smallestDelta == 0)
    {
        // process expired timer(s)
        processExpiredTimers ();

        // re-process timers with zero ticks to get smallest delta value
        smallestDelta = updateTimers (0);
    }

    return smallestDelta;
}

static void timerSignalHandler (int signum)
{
    // process the timer list here and restart based on the return value
    radUtilsSetIntervalTimer (serviceTimers (TRUE));

    return;
}


//  ... API calls

//  ... create a timer list
int radTimerListCreate
(
    int                 noTimers,
    int                 notifyDescriptor
)
{
    TIMER_ID            timer;
    UCHAR               *memory;
    int                 i;
    struct sigaction    action;

    memory = (UCHAR *)
             malloc (sizeof (*timerList) + (sizeof (*timer) * noTimers));
    if (memory == NULL)
    {
        return ERROR;
    }

    timerList = (TIMER_LIST_ID) memory;
    memset (timerList, 0, sizeof (*timerList));

    //  ... Set up the free list of timers
    timerList->noFreeTimers = noTimers;
    timerList->notifyFD     = notifyDescriptor;
    radListReset (&timerList->freeList);
    radListReset (&timerList->pendingList);

    timer = (TIMER_ID) (timerList + 1);
    for (i = 0; i < noTimers; i ++)
    {
        radListAddToEnd (&timerList->freeList, (NODE_PTR)timer);
        timer += 1;
    }

    //  ... catch SIGALRM
    memset (&action, 0, sizeof (action));
    action.sa_handler = timerSignalHandler;
    if (sigemptyset (&action.sa_mask) == -1)
    {
        free (timerList);
        return ERROR;
    }

    if (sigaction (SIGALRM, &action, NULL) == -1)
    {
        free (timerList);
        return ERROR;
    }

    return OK;
}


void radTimerListDelete
(
)
{
    radUtilsDisableSignal (SIGALRM);
    free (timerList);
}


//  ... Allocate/modify a timer, returns NULL if none are available
TIMER_ID radTimerCreate
(
    TIMER_ID        timer,
    void            (*routine) (void *parm),
    void            *parm
)
{
    if (timer == NULL)
    {
        timer = (TIMER_ID) radListRemoveFirst (&timerList->freeList);
        if (timer == NULL)
        {
            return NULL;
        }

        timerList->noFreeTimers -= 1;
        timer->pending = FALSE;
    }

    timer->routine = routine;
    timer->parm = parm;
    return timer;
}


/*  ... Deallocate a timer
*/
void radTimerDelete
(
    TIMER_ID        timer
)
{
    if (timer == NULL)
        return;

    //  ... If timer is on the pending list, remove it
    radTimerStop (timer);

    radListAddToEnd (&timerList->freeList, (NODE_PTR)timer);
    timerList->noFreeTimers += 1;
    return;
}


/*  ... put a timer on the pending list (start it)
*/
void radTimerStart
(
    TIMER_ID        timer,
    ULONG           time
)
{
    if (timer == NULL)
        return;

    radUtilsDisableSignal (SIGALRM);

    // only update the delta times and lastTick before adding the new one
    serviceTimers (FALSE);
        
    timer->deltaTime = time;

    if (timer->pending == FALSE)
    {
        // add to pending list
        timer->pending = TRUE;
        radListAddToEnd (&timerList->pendingList, (NODE_PTR)timer);
    }

    // process timers right here to avoid race conditions, then restart signal
    radUtilsSetIntervalTimer (serviceTimers (TRUE));

    radUtilsEnableSignal (SIGALRM);
    return;
}


void radTimerStop
(
    TIMER_ID        timer
)
{
    if (timer == NULL)
        return;

    radUtilsDisableSignal (SIGALRM);

    if (timer->pending == TRUE)
    {
        timer->pending = FALSE;
        radListRemove (&timerList->pendingList, (NODE_PTR)timer);
    }

    // process timers right here to avoid race conditions, then restart signal
    radUtilsSetIntervalTimer (serviceTimers (TRUE));

    radUtilsEnableSignal (SIGALRM);
}


int radTimerStatus
(
    TIMER_ID        timer
)
{
    if (timer == NULL)
        return (FALSE);

    return (timer->pending);
}


void radTimerSetUserParm
(
    TIMER_ID        timer,
    void            *newParm
)
{
    if (timer == NULL)
        return;

    radUtilsDisableSignal (SIGALRM);

    timer->parm = newParm;

    // process timers right here to avoid race conditions, then restart signal
    radUtilsSetIntervalTimer (serviceTimers (TRUE));

    radUtilsEnableSignal (SIGALRM);
    return;
}



int radTimerListDebug (void)
{
    register TIMER_ID   next;

    radMsgLog(PRI_HIGH, "################## radTimerListDebug START ##################");
    for (next = (TIMER_ID) radListGetFirst (&timerList->pendingList);
         next != NULL;
         next = (TIMER_ID) radListGetNext (&timerList->pendingList, (NODE_PTR)next))
    {
        if (next->routine)
        {
            radMsgLog(PRI_HIGH, "Timer-%8.8X: delta: %u, pending: %d, routine: %8.8X",
                       (ULONG)next, next->deltaTime, next->pending, (ULONG)next->routine);
        }
    }
    radMsgLog(PRI_HIGH, "################## radTimerListDebug  END  ##################");
}

