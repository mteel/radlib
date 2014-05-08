/*---------------------------------------------------------------------------
 
  FILENAME:
        radevents.c
 
  PURPOSE:
        Provide the events processing utilities.  
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        3/23/01         M.S. Teel       0               Original
        12/27/01        MS Teel         1               Port to linux
 
  NOTES:
        See events.h for more details.
 
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

/*  ... System include files
*/

/*  ... Library include files
*/
#include <radevents.h>


/*  ... methods
*/

EVENTS_ID radEventsInit
(
    T_QUEUE_ID      queueId,
    UINT            initialEvents,
    void            (*evtCallback) (UINT eventsRx, UINT data, void *parm),
    void            *userParm
)
{
    EVENTS_ID       newId;

    newId = (EVENTS_ID) malloc (sizeof (*newId));
    if (newId == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radEventsInit: malloc failed!");
        return NULL;
    }

    memset (newId, 0, sizeof (*newId));

    newId->qid          = queueId;
    newId->mask         = initialEvents;
    newId->evtProcessor = evtCallback;
    newId->userParm     = userParm;

    return newId;
}

/*  ... exit this subsystem
*/
void radEventsExit
(
    EVENTS_ID       id
)
{
    if (id != NULL)
    {
        free (id);
    }

    return;
}

/*  ... add new events to catch
    ... returns OK or ERROR
*/
int radEventsAdd
(
    EVENTS_ID       id,
    UINT            newEvents
)
{
    id->mask |= newEvents;

    return OK;
}

/*  ... remove events from event mask
    ... returns OK or ERROR
*/
int radEventsRemove
(
    EVENTS_ID       id,
    UINT            removeEvents
)
{
    id->mask &= ~removeEvents;

    return OK;
}

/*  ... return the current event mask
*/
UINT radEventsGetMask
    (
        EVENTS_ID       id
    )
{
    return id->mask;
}


typedef struct _notify_event_tag
{
    EVENTS_ID       id;
    UINT            eventsRx;
    UINT            data;
} SELF_EVENT;

static void selfEventsProcess (void *arg)
{
    SELF_EVENT      *se = (SELF_EVENT *)arg;
    
    (*se->id->evtProcessor) ((se->id->mask & se->eventsRx), se->data, se->id->userParm);
    
    radBufferRls (se);
}    

/*  ... send an event to another process by queue name;
    ... returns OK or ERROR
*/
int radEventsSend
(
    EVENTS_ID           id,
    char                *destName,
    UINT                eventsToSend,
    UINT                data
)
{
    EVENTS_MSG          *msg;
    int                 retVal;
    SYS_CALLBACK_MSG    cbMsg;

    if (destName == NULL)
    {
        // signalling ourself
        if (id->evtProcessor != NULL)
        {
            SELF_EVENT  *se;
            
            se = (SELF_EVENT *)radBufferGet (sizeof (SELF_EVENT));
            if (se == NULL)
                return ERROR;
            
            se->id          = id;
            se->eventsRx    = eventsToSend;
            se->data        = data;
            cbMsg.length    = sizeof (cbMsg) - sizeof (cbMsg.length);
            cbMsg.msgType   = 0;
            cbMsg.callback  = selfEventsProcess;
            cbMsg.parm      = se;
            retVal = write (radProcessGetNotifyFD (), &cbMsg, sizeof (cbMsg));
            if (retVal != sizeof (cbMsg))
            {
                radMsgLog(PRI_HIGH, "radEventsSend: write to notify fd failed: %s",
                           strerror (errno));
            }
        }

        return OK;
    }
    
    // someone else ...
    msg = (EVENTS_MSG *) radBufferGet (sizeof (*msg));
    if (msg == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radEventsSend: radBufferGet failed!");
        return ERROR;
    }

    msg->events = eventsToSend;
    msg->data   = data;

    /*  ... try to send to the destination message queue
    */
    retVal = radQueueSend (id->qid, destName, 0, msg, sizeof (*msg));
    if (retVal != OK)
    {
        radMsgLog(PRI_CATASTROPHIC, "radEventsSend: radQueueSend failed!");
        radBufferRls (msg);
        return ERROR;
    }

    return OK;
}

/*  ... this routine should be called with the result mask of a
    ... received event "message"
*/
int radEventsProcess
(
    EVENTS_ID       id,
    UINT            eventsRx,
    UINT            data
)
{
    if (id->mask & eventsRx)
    {
        (*id->evtProcessor) ((id->mask & eventsRx), data, id->userParm);
    }

    return OK;
}

