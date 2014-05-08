/*---------------------------------------------------------------------------
 
  FILENAME:
        radqueue.c
 
  PURPOSE:
        Provide the inter-task message queue utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        9/28/99         M.S. Teel       0               Original
        3/23/01         M.S. Teel       1               Port to Linux
 
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

/*  ... OS include files
*/
#include <stdio.h>
#include <stdlib.h>

/*  ... System include files
*/

/*  ... Library include files
*/
#include <radsysdefs.h>
#include <radsystem.h>
#include <radsysutils.h>

#include <raddebug.h>
#include <radqueue.h>


/*  ... global memory declarations
*/
static int          sigPipeFlag;

static T_QUEUE      queueWork;


/*  ... global memory referenced
*/

/*  ... local utilities to administer the global queue database
*/

static void sigPipeHandler (int sigNum)
{
    signal (SIGPIPE, sigPipeHandler);

    sigPipeFlag = TRUE;
    return;
}


static int qdbAddQueue
(
    T_QUEUE_ID  id,
    int         group
)
{
    int         i;

    radShmemLock (id->tableId);

    /*  ... is it already there? (avoid duplicates)
    */
    for (i = 0; i < id->queueTable->numRecs; i ++)
    {
        if (!strncmp (id->queueTable->recs[i].name, id->name, QUEUE_NAME_LENGTH) &&
                id->queueTable->recs[i].group == group)
        {
            radShmemUnlock (id->tableId);
            return OK;
        }
    }

    if (id->queueTable->numRecs >= MAX_QUEUE_RECORDS)
    {
        radMsgLog(PRI_MEDIUM, "qdbAddQueue: queue table full!");
        radShmemUnlock (id->tableId);
        return ERROR;
    }

    strncpy (id->queueTable->recs[id->queueTable->numRecs].name, id->name, QUEUE_NAME_LENGTH);
    id->queueTable->recs[id->queueTable->numRecs].group      = group;
    id->queueTable->recs[id->queueTable->numRecs].updateFlag = 1;


    /*  ... set the update flag for all other group members
    */
    for (i = 0; i < id->queueTable->numRecs; i ++)
    {
        if (id->queueTable->recs[i].group == group)
        {
            id->queueTable->recs[i].updateFlag = 1;
        }
    }

    id->queueTable->numRecs ++;

    radShmemUnlock (id->tableId);
    return OK;
}

static int qdbDeleteQueue
(
    T_QUEUE_ID  id,
    int         group
)
{
    int         i, j, foundFlag = FALSE;

    radShmemLock (id->tableId);

    for (i = 0; i < id->queueTable->numRecs; i ++)
    {
        if (id->queueTable->recs[i].group == group)
        {
            id->queueTable->recs[i].updateFlag = 1;
        }

        if (!strncmp (id->queueTable->recs[i].name, id->name, QUEUE_NAME_LENGTH) &&
                (id->queueTable->recs[i].group == group || group == QUEUE_GROUP_ALL))
        {
            foundFlag = TRUE;
            for (j = i; j < id->queueTable->numRecs - 1; j ++)
            {
                id->queueTable->recs[j] = id->queueTable->recs[j + 1];
            }
            id->queueTable->numRecs --;

            if (group == QUEUE_GROUP_ALL)
            {
                continue;
            }
            else
            {
                radShmemUnlock (id->tableId);
                return OK;
            }
        }
    }

    radShmemUnlock (id->tableId);

    if (foundFlag)
        return OK;
    else
        return ERROR;
}

/*  ... some traversal utils
*/
static char *qdbGetNextGroupName (T_QUEUE_ID id, int *currentIndex, int group, char *store)
{
    int         i;

    radShmemLock (id->tableId);

    for (i = *currentIndex + 1; i < id->queueTable->numRecs; i ++)
    {
        if (id->queueTable->recs[i].group == group || group == QUEUE_GROUP_ALL)
        {
            *currentIndex = i;
            strncpy (store,  id->queueTable->recs[i].name,  QUEUE_NAME_LENGTH);
            radShmemUnlock (id->tableId);
            return store;
        }
    }

    radShmemUnlock (id->tableId);
    return NULL;
}

/*  ... by calling this, if the updateFlag is set, it will be cleared!
*/
static int qdbIsUpdateFlagSet (T_QUEUE_ID id, int group)
{
    int         i, retVal;

    radShmemLock (id->tableId);

    for (i = 0; i < id->queueTable->numRecs; i ++)
    {
        if (id->queueTable->recs[i].group == group &&
                !strncmp (id->queueTable->recs[i].name, id->name, QUEUE_NAME_LENGTH))
        {
            if (id->queueTable->recs[i].updateFlag != 0)
            {
                id->queueTable->recs[i].updateFlag = 0;
                retVal = TRUE;
            }
            else
            {
                retVal = FALSE;
            }

            radShmemUnlock (id->tableId);
            return retVal;
        }
    }

    radShmemUnlock (id->tableId);
    return FALSE;
}


/*  ... attach to a group of queues based on group number
    ... so that messages can be sent to the group
    ... returns OK or ERROR
*/
static int radQueueAttachGroup
(
    T_QUEUE_ID  tqid,
    int         newGroupNumber
)
{
    int         index = 0;
    char      store[QUEUE_NAME_LENGTH+1];

    while (qdbGetNextGroupName (tqid, &index, newGroupNumber, store) != NULL)
    {
        if (!strncmp (store, tqid->name, QUEUE_NAME_LENGTH))
        {
            /*  ... it's me - skip it!
            */
            continue;
        }

        if (radQueueAttach (tqid, store, newGroupNumber) == ERROR)
        {
            radMsgLog(PRI_MEDIUM, "radQueueAttachGroup: radQueueAttach failed!");
            return ERROR;
        }
    }

    return OK;
}


/*  ... dettach from a group of queues based on group number
*/
static int radQueueDettachGroup
(
    T_QUEUE_ID  tqid,
    int         oldGroupNumber
)
{
    int         index = 0;
    char      store[QUEUE_NAME_LENGTH+1];

    while (qdbGetNextGroupName (tqid, &index, oldGroupNumber, store) != NULL)
    {
        if (!strncmp (store, tqid->name, QUEUE_NAME_LENGTH))
        {
            /*  ... it's me - skip it!
            */
            continue;
        }

        if (radQueueDettach (tqid, store, oldGroupNumber) == ERROR)
        {
            radMsgLog(PRI_MEDIUM, "radQueueDettachGroup: name %s not found!", store);
        }
    }

    return OK;
}

/*  ... dettach from all send queues
*/
void radQueueFreeSendList
(
    T_QUEUE_ID  tqid
)
{
    QSEND_NODE  *node;

    for (node = (QSEND_NODE *) radListGetFirst (&tqid->sendQueues);
            node != NULL;
            node = (QSEND_NODE *) radListGetFirst (&tqid->sendQueues) )
    {
        /*  ... lose him!
        */
        radListRemove (&tqid->sendQueues, (NODE_PTR)node);
        radBufferRls (node);
    }

    return;
}

static int qSendListGetFD (T_QUEUE_ID tqid, char *name)
{
    QSEND_NODE  *node;

    if (!strncmp (tqid->name, name, QUEUE_NAME_LENGTH))
    {
        /*  ... it's our own queue! - send to the reflector pipe
        */
        return tqid->reflectFD;
    }

    for (node = (QSEND_NODE *) radListGetFirst (&tqid->sendQueues);
            node != NULL;
            node = (QSEND_NODE *) radListGetNext (&tqid->sendQueues, (NODE_PTR)node))
    {
        if (!strncmp (node->name, name, QUEUE_NAME_LENGTH))
        {
            /*  ... found him!
            */
            return node->pipeFD;
        }
    }

    return -1;
}

#if 0
static void qSendListDebugDump (T_QUEUE_ID tqid)
{
    QSEND_NODE  *node;

    printdbg (0, "---- Dumping send list for %s ----\n",
              tqid->name);

    for (node = (QSEND_NODE *) radListGetFirst (&tqid->sendQueues);
            node != NULL;
            node = (QSEND_NODE *) radListGetNext (&tqid->sendQueues, (NODE_PTR)node))
    {
        printdbg (0, "name=%s, pipeFD=%s, grp=%d\n",
                  node->name, node->pipeFD, node->group);
    }

    return;
}
#endif

static int qSendListUpdate (T_QUEUE_ID tqid, int group)
{
    int         index = 0;
    char      store[QUEUE_NAME_LENGTH+1];

    /*  ... check for new guys
    */
    while (qdbGetNextGroupName (tqid, &index, group, store) != 0)
    {
        if (!strncmp (store, tqid->name, QUEUE_NAME_LENGTH))
        {
            /*  ... it's me - skip it!
            */
            continue;
        }

        /*  ... is this a new guy?
        */
        if (qSendListGetFD (tqid, store) == -1)
        {
            /*  ... YES!
            */
            if (radQueueAttach (tqid, store, group) == ERROR)
            {
                radMsgLog(PRI_MEDIUM, "qSendListUpdate: radQueueAttach failed!");
                return ERROR;
            }
        }
    }

    return OK;
}


/*  ... the dummy child process
*/
static void dummyChild (char *myName)
{
    int         pipeFD, reflectFD;
    char        buffer[256];
    int         bytesWritten, bytesRead, retVal;

    sprintf (buffer, "%sREF", myName);

    if ((reflectFD = open (buffer, O_RDONLY)) == -1)
    {
        printf ("dummyChild: reflector open failed: %s", strerror (errno));
        exit (1);
    }

    if ((pipeFD = open (myName, O_WRONLY)) == -1)
    {
        printf ("dummyChild: open failed: %s\n", strerror (errno));
        exit (1);
    }

    /*  ... sign up for the SIGPIPE signal (reader leaves writers hanging)
    */
    sigPipeFlag = 0;
    signal (SIGPIPE, sigPipeHandler);

    for (;;)
    {
        bytesRead = read (reflectFD, buffer, sizeof (buffer));
        if (bytesRead == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }

            close (pipeFD);
            close (reflectFD);
            exit (1);
        }
        else if (bytesRead == 0)
        {
            close (pipeFD);
            close (reflectFD);
            exit (0);
        }

        /* reflect data back to my master process
        */
        bytesWritten = 0;
        while (bytesWritten < bytesRead)
        {
            retVal = write (pipeFD,
                            (void *)&buffer[bytesWritten],
                            bytesRead - bytesWritten);
            if (sigPipeFlag)
            {
                sigPipeFlag = 0;
                printf ("dummyChild: reader gone on fd %d", pipeFD);
                close (pipeFD);
                close (reflectFD);
                exit (1);
            }
            else if (retVal == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                printf ("dummyChild: write failed on fd %d: %s",
                        pipeFD, strerror (errno));
                close (pipeFD);
                close (reflectFD);
                exit (1);
            }
            else
            {
                bytesWritten += retVal;
            }
        }
    }
}



/*  ... API calls
*/

int radQueueSystemInit (int initFlag)
{
    memset (&queueWork, 0, sizeof (queueWork));

    /*  ... create/attach the queue table
    */
    if ((queueWork.tableId = radShmemInit (KEY_MSGQ,
                                           SEM_INDEX_MSGQ,
                                           sizeof (MSGQ_TABLE)))
            == NULL)
    {
        return ERROR;
    }

    queueWork.queueTable = (MSGQ_TABLE *) radShmemGet (queueWork.tableId);

    if (initFlag)
    {
        radShmemLock (queueWork.tableId);
        memset (queueWork.queueTable, 0, sizeof (MSGQ_TABLE));
        radShmemUnlock (queueWork.tableId);
    }

    return OK;
}


T_QUEUE_ID radQueueInit
(
    char            *myName,
    int             startDummyProc
)
{
    T_QUEUE_ID      newId;
    int             retVal, initFlag = FALSE;
    char            temp[128];

    newId = &queueWork;


    /*  ... create my queue pipe
    */
    if (mkfifo (myName, 0664) == -1 && errno != EEXIST)
    {
        radMsgLog(PRI_HIGH, "radQueueInit: mkfifo failed: %s", strerror (errno));
        return NULL;
    }

    /*  ... does the caller want me to start a dummy task to do nothing
        ... more than open his pipe for writing?
        ... (This prevents the caller from blocking on the "open" below)
    */
    if (startDummyProc)
    {
        /*  ... create my reflector pipe
        */
        sprintf (temp, "%sREF", myName);
        if (mkfifo (temp, 0664) == -1 &&
                errno != EEXIST)
        {
            radMsgLog(PRI_HIGH, "reflector mkfifo failed: %s", strerror (errno));
            return NULL;
        }

        retVal = fork ();
        if (retVal == -1)
        {
            radMsgLog(PRI_HIGH, "radQueueInit: dummyProc fork failed: %s", strerror (errno));
            return NULL;
        }
        else if (retVal == 0)
        {
            /* ... child
            */
            /*  ... assign the signal handlers to default
            */
            signal (SIGABRT, SIG_DFL);
            signal (SIGINT, SIG_DFL);
            signal (SIGQUIT, SIG_DFL);
            signal (SIGTERM, SIG_DFL);
            signal (SIGTSTP, SIG_DFL);
            signal (SIGCHLD, SIG_DFL);

            /* ... close stdin
            */
            fclose (stdin);

            dummyChild (myName);
            exit (0);
        }
        else
        {
            /* ... parent
            */
            newId->dummyPid = retVal;

            if ((newId->reflectFD = open (temp, O_WRONLY)) == -1)
            {
                radMsgLog(PRI_HIGH, "radQueueInit: reflector open failed: %s", strerror (errno));
                return NULL;
            }
        }
    }

    if ((newId->pipeFD = open (myName, O_RDONLY)) == -1)
    {
        close (newId->reflectFD);
        radMsgLog(PRI_HIGH, "radQueueInit: open failed: %s", strerror (errno));
        return NULL;
    }

    strncpy (newId->name, myName, QUEUE_NAME_LENGTH);
    strncpy (newId->refName, temp, QUEUE_NAME_LENGTH);
    radListReset (&newId->sendQueues);

    /*  ... add my queue to the global table
    */
    if (qdbAddQueue (newId, QUEUE_GROUP_ALL) == ERROR)
    {
        close (newId->reflectFD);
        close (newId->pipeFD);
        return NULL;
    }

    /*  ... sign up for the SIGPIPE signal (reader leaves writers hanging)
    */
    signal (SIGPIPE, sigPipeHandler);


    /*  ... we're happy!
    */
    return newId;
}


void radQueueExit
(
    T_QUEUE_ID      id
)
{
    radQueueFreeSendList (id);
    qdbDeleteQueue (id, QUEUE_GROUP_ALL);
    close (id->reflectFD);
    close (id->pipeFD);

    if (id->dummyPid != 0)
    {
        kill (id->dummyPid, SIGKILL);
    }

    return;
}


void radQueueSystemExit (int destroy)
{
    if (destroy)
    {
        radShmemExitAndDestroy (queueWork.tableId);
    }
    else
    {
        radShmemExit (queueWork.tableId);
    }

    return;
}



/*  ... attach to an individual queue based on queue name
    ... so that messages can be sent to it
    ... returns OK or ERROR
*/
int radQueueAttach
(
    T_QUEUE_ID  tqid,
    char        *newQueueName,
    int         group
)
{
    QSEND_NODE  *node;

    for (node = (QSEND_NODE *) radListGetFirst (&tqid->sendQueues);
            node != NULL;
            node = (QSEND_NODE *) radListGetNext (&tqid->sendQueues, (NODE_PTR)node))
    {
        if (!strncmp (newQueueName, node->name, QUEUE_NAME_LENGTH) &&
                node->group == group)
        {
            /*  ... he's already in the list!
            */
            return OK;
        }
    }

    /*  ... add this guy
    */
    node = (QSEND_NODE *) radBufferGet (sizeof (*node));
    if (node == NULL)
    {
        radMsgLog(PRI_MEDIUM, "radQueueAttach: radBufferGet failed to create send node!");
        return ERROR;
    }

    strncpy (node->name, newQueueName, QUEUE_NAME_LENGTH);
    node->group = group;


    /*  ... attach to his msg queue pipe
    */
    node->pipeFD = open (newQueueName, O_WRONLY);
    if (node->pipeFD == -1)
    {
        radMsgLog(PRI_MEDIUM, "radQueueAttach: open %s failed: %s", newQueueName, strerror (errno));
        radBufferRls (node);
        return ERROR;
    }

    /* ... add to the list
    */
    radListAddToEnd (&tqid->sendQueues, (NODE_PTR)node);

    return OK;
}

/*  ... dettach from an individual queue based on queue key
*/
int radQueueDettach
(
    T_QUEUE_ID  tqid,
    char        *oldQueueName,
    int         group
)
{
    QSEND_NODE  *node;

    for (node = (QSEND_NODE *) radListGetFirst (&tqid->sendQueues);
            node != NULL;
            node = (QSEND_NODE *) radListGetNext (&tqid->sendQueues, (NODE_PTR)node))
    {
        if (!strncmp (oldQueueName, node->name, QUEUE_NAME_LENGTH) &&
                node->group == group)
        {
            /*  ... lose him!
            */
            radListRemove (&tqid->sendQueues, (NODE_PTR)node);
            close (node->pipeFD);
            radBufferRls (node);
            return OK;
        }
    }

    return ERROR;
}

/*  ... add my queue to a group
    ... returns OK or ERROR
*/
int radQueueJoinGroup
(
    T_QUEUE_ID  tqid,
    int         groupNumber
)
{
    /*  ... add my queue to the global table
    */
    if (qdbAddQueue (tqid, groupNumber) == ERROR)
    {
        radMsgLog(PRI_MEDIUM, "radQueueJoinGroup: qdbAddQueue failed!");
        return ERROR;
    }

    /*  ... add all group entries in the global list to my address list
    */
    if (radQueueAttachGroup (tqid, groupNumber) == ERROR)
    {
        qdbDeleteQueue (tqid, groupNumber);
        radMsgLog(PRI_MEDIUM, "radQueueJoinGroup: radQueueAttachGroup failed!");
        return ERROR;
    }

    return OK;
}

/*  ... remove my queue from a group
    ... returns OK or ERROR
*/
int radQueueQuitGroup
(
    T_QUEUE_ID  tqid,
    int         groupNumber
)
{
    /*  ... remove all group entries in my address list
    */
    if (radQueueDettachGroup (tqid, groupNumber) == ERROR)
    {
        radMsgLog(PRI_MEDIUM, "radQueueQuitGroup: radQueueDettachGroup failed!");
        return ERROR;
    }

    /*  ... remove my queue from the global table
    */
    if (qdbDeleteQueue (tqid, groupNumber) == ERROR)
    {
        radMsgLog(PRI_MEDIUM, "radQueueQuitGroup: qdbDeleteQueue failed!");
        return ERROR;
    }

    return OK;
}


/*  ... read from msg queue
    ... populates (srcQueueName, msg, length, msgType)
    ... NOTE: msg will point to the system buffer when this call
    ... returns.  User MUST call radBufferRls when done with buffer!
    ... RETURNS: TRUE if msg received, FALSE if queue is empty, ERROR if error
*/
int radQueueRecv
(
    T_QUEUE_ID          tqid,
    char                *srcQueueName,
    UINT                *msgType,
    void                **msg,
    UINT                *length
)
{
    int                 retVal;
    QMSG_HDR            hdr;
    UCHAR               *bPtr = (UCHAR *)&hdr;
    int                 bytesRead = 0;

    while (bytesRead < sizeof(hdr))
    {
        retVal = read (tqid->pipeFD, bPtr + bytesRead, sizeof(hdr) - bytesRead);
        if (retVal < 0)
        {
            /*  ... read error
            */
            if (errno == EAGAIN || errno == EINTR)
            {
                radUtilsSleep (1);
                continue;
            }
            else
            {
                radMsgLog(PRI_MEDIUM, "radQueueRecv: read failed: %s", strerror (errno));
                return FALSE;
            }
        }
        else if (retVal == 0)
        {
            close (tqid->pipeFD);
            radMsgLog(PRI_HIGH, "radQueueRecv: no writers to %s pipe - closing it!", tqid->name);
            return ERROR;
        }

        bytesRead += retVal;
    }


    strncpy (srcQueueName, hdr.name, QUEUE_NAME_LENGTH);
    *msgType        = hdr.mtype;
    *length         = hdr.length;

    if (hdr.length != 0)
    {
        *msg        = radBufferGetPtr (hdr.bfrOffset);
    }
    else
    {
        *msg        = 0;
    }

    return TRUE;
}

/*  ... write to a queue
    ... assumes sysBuffer is a valid pointer to a system buffer
    ... system buffer ownership is transfered to the receiving queue
    ... returns OK, ERROR, or ERROR_ABORT if the dest queue is gone
    ... user should dettach from a dest on ERROR_ABORT!
*/
int radQueueSend
(
    T_QUEUE_ID  tqid,
    char        *destQueueName,
    UINT        msgType,
    void        *sysBuffer,
    UINT        length
)
{
    int         retVal, destFD;
    QMSG_HDR    hdr;

    /*  ... get the dest FD
    */
    if ((destFD = qSendListGetFD (tqid, destQueueName)) == -1)
    {
        radMsgLog(PRI_MEDIUM, "radQueueSend: qSendListGetFD failed for %s!",
                   destQueueName);
        return ERROR;
    }

    hdr.mtype           = msgType;
    strncpy (hdr.name, tqid->name, QUEUE_NAME_LENGTH);
    hdr.length          = length;

    if (length != 0)
    {
        hdr.bfrOffset   = radBufferGetOffset (sysBuffer);
    }
    else
    {
        hdr.bfrOffset   = 0;
    }

    retVal = write (destFD, (void *)&hdr, sizeof (hdr));
    if (sigPipeFlag)
    {
        sigPipeFlag = 0;
        radMsgLog(PRI_MEDIUM, "radQueueSend: reader gone on fd %d", destFD);
        return ERROR_ABORT;
    }
    else if (retVal == -1)
    {
        radMsgLog(PRI_MEDIUM, "radQueueSend: write failed on fd %d: %s", destFD, strerror (errno));
        return ERROR;
    }
    else if (retVal != sizeof (hdr))
    {
        radMsgLog(PRI_MEDIUM, "radQueueSend: wrote %d of %d bytes", retVal, sizeof (hdr));
        return ERROR;
    }

    return OK;
}

/*  ... write to all queues in a group
    ... assumes sysBuffer is a valid pointer to a system buffer
    ... system buffer ownership is transfered to the receiving queue
    ... returns OK or ERROR
*/
int radQueueSendGroup
(
    T_QUEUE_ID  tqid,
    int         destGroup,
    UINT        msgType,
    void        *sysBuffer,
    UINT        length
)
{
    int         index = 0;
    UCHAR       *newBfr;
    char        store[QUEUE_NAME_LENGTH+1];

    /*  ... has our group changed?
    */
    if (qdbIsUpdateFlagSet (tqid, destGroup) == TRUE)
    {
        if (qSendListUpdate (tqid, destGroup) == ERROR)
        {
            radMsgLog(PRI_MEDIUM, "radQueueSendGroup: qSendListUpdate failed!");
            if (length != 0)
            {
                radBufferRls (sysBuffer);
            }
            return ERROR;
        }
    }


    while (qdbGetNextGroupName (tqid, &index, destGroup, store) != NULL)
    {
        if (!strncmp (store, tqid->name, QUEUE_NAME_LENGTH))
        {
            /*  ... it's me - skip it!
            */
            continue;
        }

        newBfr = NULL;
        if (length > 0)
        {
            newBfr = (UCHAR *) radBufferGet (length);
            if (newBfr == NULL)
            {
                radMsgLog(PRI_MEDIUM, "radQueueSendGroup: radBufferGet failed!");
                radBufferRls (sysBuffer);
                return ERROR;
            }

            memcpy (newBfr, sysBuffer, length);
        }

        if (radQueueSend (tqid, store, msgType, (void *)newBfr, length) == ERROR)
        {
            radMsgLog(PRI_MEDIUM, "radQueueSendGroup: %s radQueueSend failed!",
                       store);
            if (length > 0)
            {
                radBufferRls (newBfr);
            }
        }
    }

    if (length != 0)
    {
        radBufferRls (sysBuffer);
    }
    return OK;
}

int radQueueIsAttached
(
    T_QUEUE_ID  tqid,
    char        *queueName
)
{
    if (qSendListGetFD (tqid, queueName) != -1)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

char *radQueueGetName
(
    T_QUEUE_ID  tqid,
    char        *store
)
{
    strncpy (store, tqid->name, QUEUE_NAME_LENGTH);
    return store;
}

int radQueueGetFD
(
    T_QUEUE_ID  tqid
)
{
    return tqid->pipeFD;
}

