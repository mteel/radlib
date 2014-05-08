/*---------------------------------------------------------------------------
 
  FILENAME:
        radprocess.c
 
  PURPOSE:
     Provide an asynchronous event-driven process control utility.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Function
        12/27/01        M.S. Teel       0               Original
 
  NOTES:
        See process.h.
 
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

/*  ... Local include files
*/
#include <radprocess.h>


/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/
static PROCESS_DATA     procData;


/* ... methods
*/

/*  ... static utilities
*/

/*  ... allocate an IO block;
    ... returns OK or ERROR
*/
static int procAllocIOBlock
(
    int     fdIndex,
    int     fd,
    void    (*ioCallback) (int fd, void *user),
    void    *userData
)
{
    if (fdIndex < PROC_FD_PIPE_READ || fdIndex > PROC_FD_USER_LAST)
    {
        return ERROR;
    }

    procData.ioIDs[fdIndex].ioCallback  = ioCallback;
    procData.ioIDs[fdIndex].userData    = userData;

    procData.fds[fdIndex] = fd;

    if (fd > procData.fdMax)
    {
        procData.fdMax = fd;
    }
    FD_SET(fd, &procData.fdSet);

    return OK;
}

/*  ... free an IO block
*/
static void procFreeIOBlock (int fdIndex)
{
    int         i;

    FD_CLR(procData.fds[fdIndex], &procData.fdSet);
    if (procData.fdMax == procData.fds[fdIndex])
    {
        /*      ... reset the max fd
        */
        procData.fdMax = 0;
        for (i = 0; i < PROC_FD_NUM_INDEXES; i ++)
        {
            if (i == fdIndex)
            {
                continue;
            }

            if (procData.fds[i] > procData.fdMax)
            {
                procData.fdMax = procData.fds[i];
            }
        }
    }

    memset (&procData.ioIDs[fdIndex], 0, sizeof (procData.ioIDs[fdIndex]));
    procData.fds[fdIndex] = -1;
    return;
}


static void procPipeReadCB (int fd, void *userData)
{
    SYS_CALLBACK_MSG    msg;
    int                 retVal;

    retVal = read (fd, &msg, sizeof (msg));
    if (retVal == -1)
    {
        radMsgLog(PRI_HIGH, "procPipeReadCB: read on pipe failed!");
        return;
    }
    else if (retVal != sizeof (msg))
    {
        radMsgLog(PRI_HIGH, "procPipeReadCB: partial read on pipe!");
        return;
    }

    (*msg.callback) (msg.parm);
    return;
}

static void procQueueReadCB (int fd, void *userData)
{
    char                srcQName[QUEUE_NAME_LENGTH+1];
    UINT                msgType;
    UINT                length;
    void                *recvBfr;
    int                 retVal;
    EVENTS_MSG          *evtMsg;
    PROC_MSGQ_HANDLER   *node;

    if ((retVal = radQueueRecv (procData.myQueue,
                                srcQName,
                                &msgType,
                                &recvBfr,
                                &length))
            == FALSE)
    {
        radMsgLog(PRI_STATUS, "procQueueReadCB: woke on queue - no msg there!");
        return;
    }
    else if (retVal == ERROR)
    {
        radMsgLog(PRI_STATUS, "procQueueReadCB: queue is closed!");
        procData.exitFlag = TRUE;
        return;
    }

    /*  ... is this an EVENT message (msgType == 0)?
    */
    if (msgType == 0)
    {
        /*  ... Yes! Process the events ...
        */
        evtMsg = (EVENTS_MSG *)recvBfr;

        radEventsProcess (procData.events, evtMsg->events, evtMsg->data);
    }
    else
    {
        for (node = (PROC_MSGQ_HANDLER *)radListGetFirst (&procData.msgqHandlerList);
             node != NULL;
             node = (PROC_MSGQ_HANDLER *)radListGetNext (&procData.msgqHandlerList,
                                                         (NODE_PTR)node))
        {
            if (node->msgHandler != NULL)
            {
                /*  ... pass it on to the user's handler ...
                */
                procData.keepMsgQBuffer = FALSE;
                procData.stopMsqQHandlerTraversal = FALSE;

                (*node->msgHandler) (srcQName, msgType, recvBfr, length, node->udata);

                /*  ... check for any flags that may have been set in the message
                    ... handler ...
                */
                if (procData.keepMsgQBuffer)
                {
                    /* just bail out here */
                    return;
                }
                else if (procData.stopMsqQHandlerTraversal)
                {
                    break;
                }
            }
        }
    }

    /*  ... allow for zero-length messages
    */
    if (length > 0 && recvBfr)
    {
        radBufferRls (recvBfr);
    }

    return;
}


/*  ... initialize process management; called once during process init;
    ... automatically sets up the following utilities for a new process:
    ...     Semaphores (semaphore.h)
    ...     System Buffers (buffers.h)
    ...     MsgLog (radMsgLog.h)
    ...     Message Queue (queue.h)
    ...     IO processing ("select")
    ...     Timer List with 'numTimers' timers (timers.h)
    ...     Event handler (events.h)
    ...
    ... 'processName' is used for logging;
    ... 'queueName' is used for the msg queue and events;
    ... 'queueDbKey' is used to attach to the proper shared queue groups
    ...     and queue database;
    ... 'queueDbSemIndex' is necessary for mutex protection on the
    ...     shared queue groups and queue database;
    ... 'numTimers' determines the max number of timers for this process;
    ... 'runAsDaemon' determines if radMsgLog's go to stderr or not and
    ...     whether or not to close stdin, stdout, and stderr;
    ... 'messageHandler' is the callback for queue messages;
    ... 'eventHandler' is the callback for events;
    ... 'userData' is passed to the event and default message callbacks;
    ...
    ... returns OK or ERROR
*/
int radProcessInit
(
    char    *processName,
    char    *queueName,
    int     numTimers,
    int     runAsDaemon,
    void    (*messageHandler)   (
                                char *srcQueueName,
                                UINT msgType,
                                void *msg,
                                UINT length,
                                void *userData
                                ),
    void    (*eventHandler)     (
                                UINT eventsRx,
                                UINT rxData,
                                void *userData
                                ),
    void    *userData
)
{
    int     i;
    char    temp [512];

    /*  ... if daemon, become one; start the radMsgLogin either case
    */
    if (runAsDaemon == TRUE)
    {
        if (getcwd(temp, 511) == NULL)
        {
            radMsgLogInit (processName, FALSE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "radProcessInit: getcwd returned NULL: %s",
                       strerror(errno));
            radMsgLogExit ();
            return ERROR;
        }

        radUtilsBecomeDaemon (temp);
        radMsgLogInit (processName, FALSE, TRUE);
    }
    else
    {
        radMsgLogInit (processName, TRUE, TRUE);
    }


    if (processName == NULL ||
        queueName == NULL ||
        messageHandler == NULL ||
        eventHandler == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radProcessInit: NULL pointer given!");
        radMsgLogExit ();
        return ERROR;
    }

    memset (&procData, 0, sizeof (procData));

    for (i = 0; i < PROC_FD_NUM_INDEXES; i ++)
    {
        procData.fds[i] = -1;
    }

    strncpy (procData.name, processName, PROCESS_MAX_NAME_LEN);

    procData.pid = getpid ();
    procData.userData = userData;

    radListReset (&procData.msgqHandlerList);
    procData.defaultMsgQID = radProcessQueuePrependHandler (messageHandler, userData);

    /*  ... init the file descriptor set
    */
    FD_ZERO(&procData.fdSet);


    /*  ... create the notification pipes
    */
    if (pipe (procData.fds) != 0)
    {
        radMsgLog(PRI_CATASTROPHIC, "radProcessInit: pipe failed!");
        radProcessQueueRemoveHandler (procData.defaultMsgQID);
        radMsgLogExit ();
        return ERROR;
    }
    if (procAllocIOBlock (PROC_FD_PIPE_READ,
                          procData.fds[PROC_FD_PIPE_READ],
                          procPipeReadCB,
                          &procData)
        == ERROR)
    {
        radMsgLog(PRI_CATASTROPHIC, "radProcessInit: procAllocIOBlock failed!\n");
        close (procData.fds[PROC_FD_PIPE_READ]);
        close (procData.fds[PROC_FD_PIPE_WRITE]);
        radProcessQueueRemoveHandler (procData.defaultMsgQID);
        radMsgLogExit ();
        return ERROR;
    }


    /*  ... create the message queue
    */
    procData.myQueue = radQueueInit (queueName, TRUE);
    if (procData.myQueue == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radProcessInit: radQueueInit failed!\n");
        close (procData.fds[PROC_FD_PIPE_READ]);
        close (procData.fds[PROC_FD_PIPE_WRITE]);
        radProcessQueueRemoveHandler (procData.defaultMsgQID);
        radMsgLogExit ();
        return ERROR;
    }
    if (procAllocIOBlock (PROC_FD_MSG_QUEUE,
                          radQueueGetFD (procData.myQueue),
                          procQueueReadCB,
                          &procData)
        == ERROR)
    {
        radMsgLog(PRI_CATASTROPHIC, "radProcessInit: procAllocIOBlock failed!\n");
        close (procData.fds[PROC_FD_PIPE_READ]);
        close (procData.fds[PROC_FD_PIPE_WRITE]);
        radQueueExit (procData.myQueue);
        radProcessQueueRemoveHandler (procData.defaultMsgQID);
        radMsgLogExit ();
        return ERROR;
    }


    /*  ... init the event handling
    */
    procData.events = radEventsInit (procData.myQueue, 0, eventHandler, userData);
    if (procData.events == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radProcessInit: radEventsInit failed!\n");
        close (procData.fds[PROC_FD_PIPE_READ]);
        close (procData.fds[PROC_FD_PIPE_WRITE]);
        radQueueExit (procData.myQueue);
        radProcessQueueRemoveHandler (procData.defaultMsgQID);
        radMsgLogExit ();
        return ERROR;
    }


    /*  ... init the timerList
    */
    if (numTimers > 0)
    {
        if (radTimerListCreate (numTimers, procData.fds[PROC_FD_PIPE_WRITE])
            == ERROR)
        {
            radMsgLog(PRI_CATASTROPHIC, "radProcessInit: radTimerListCreate failed!\n");
            close (procData.fds[PROC_FD_PIPE_READ]);
            close (procData.fds[PROC_FD_PIPE_WRITE]);
            radEventsExit (procData.events);
            radQueueExit (procData.myQueue);
            radProcessQueueRemoveHandler (procData.defaultMsgQID);
            radMsgLogExit ();
            return ERROR;
        }
    }

    radMsgLog(PRI_STATUS, "radlib: %s started %s",
               processName, ((runAsDaemon) ? "as a daemon ..." : "..."));

    return OK;
}



void radProcessExit (void)
{
    radProcessQueueRemoveHandler (procData.defaultMsgQID);
    radTimerListDelete ();
    radEventsExit (procData.events);
    radQueueExit (procData.myQueue);
    radMsgLogExit ();
    close (procData.fds[PROC_FD_PIPE_READ]);
    close (procData.fds[PROC_FD_PIPE_WRITE]);

    return;
}


void radProcessSetExitFlag (void)
{
    procData.exitFlag = TRUE;
}

int radProcessGetExitFlag (void)
{
    return procData.exitFlag;
}


/*  ... wait for messages, timers and events in one call;
    ... should be the focal point of a process's main loop;
    ... 'timeout' (in milliseconds), if > 0, will cause this function to 
    ... return OK even if no I/O triggered after 'timeout' milliseconds 
    ... (remember linux PC timers are accurate to 10 ms typically);
    ... returns OK or ERROR
*/
int radProcessWait (int timeout)
{
    fd_set          readFds;
    int             i, retVal;
    struct timeval  tv;

    if (procData.exitFlag)
    {
        radMsgLog(PRI_HIGH, "radProcessWait: exit flag is set!");
        return ERROR;
    }

    readFds = procData.fdSet;

    if (timeout > 0)
    {
        tv.tv_sec   = timeout/1000;
        tv.tv_usec  = (timeout%1000) * 1000;
        retVal = select (procData.fdMax+1, &readFds, NULL, NULL, &tv);
    }
    else
    {
        retVal = select (procData.fdMax+1, &readFds, NULL, NULL, NULL);
    }

    if (retVal == -1)
    {
        if (errno == EINTR)
        {
            if (procData.exitFlag)
            {
                return ERROR;
            }
            else
            {
                return OK;
            }
        }
        else
        {
            radMsgLog(PRI_MEDIUM, "radProcessWait: select call: %s",
                       strerror (errno));
            procData.exitFlag = TRUE;
            return ERROR;
        }
    }
    else if (retVal == 0)
    {
        return TIMEOUT;
    }


    /*  ... figure out which birds are chirping ...
    */
    for (i = 0; i < PROC_FD_NUM_INDEXES; i ++)
    {
        if (procData.fds[i] == -1)
        {
            continue;
        }
        if (FD_ISSET(procData.fds[i], &readFds))
        {
            /* ... run the IO callback
            */
            if (procData.ioIDs[i].ioCallback != NULL)
            {
                (*procData.ioIDs[i].ioCallback)
                    (procData.fds[i], procData.ioIDs[i].userData);
            }
        }
    }

    return OK;
}



/*  *** general process utilities ***
*/
/*  ... get the calling process's name;
    ... returns the pointer 'store', where the name is copied
*/
char *radProcessGetName (char *store)
{
    strncpy (store, procData.name, PROCESS_MAX_NAME_LEN);

    return store;
}


/*  ... get the calling process's pid
*/
pid_t radProcessGetPid (void)
{
    return procData.pid;
}

/*  ... get the calling process's notify fd
*/
int radProcessGetNotifyFD (void)
{
    return procData.fds[PROC_FD_PIPE_WRITE];
}


/*  *** process I/O utilities ***
*/
/*  ... register your file descriptor for "radProcessWait" inclusion;
    ... 'ioCallback' will be executed if data or an error occurs on 'fd';
    ... 'userData will be passed to 'ioCallback';
    ... returns PROC_IO_ID or ERROR
*/
PROC_IO_ID radProcessIORegisterDescriptor
(
    int         fd,
    void        (*ioCallback) (int fd, void *userData),
    void        *userData
)
{
    int         i, retVal;

    for (i = PROC_FD_USER_FIRST; i <= PROC_FD_USER_LAST; i ++)
    {
        if (procData.fds[i] != -1)
        {
            continue;
        }

        retVal = procAllocIOBlock (i, fd, ioCallback, userData);
        if (retVal != OK)
        {
            return ERROR;
        }
        else
        {
            return i;
        }
    }

    return ERROR;
}

/*  ... de-register your file descriptor for "radProcessWait" inclusion;
    ... returns OK or ERROR
*/
int radProcessIODeRegisterDescriptor
(
    PROC_IO_ID  id
)
{
    if (id < PROC_FD_USER_FIRST || id > PROC_FD_USER_LAST || procData.fds[id] == -1)
    {
        return ERROR;
    }

    procFreeIOBlock (id);

    return OK;
}

int radProcessIODeRegisterDescriptorByFd
(
    int     fd
)
{
    int     index;

    for (index = PROC_FD_USER_FIRST; index < PROC_FD_USER_LAST; index ++)
    {
        if (procData.fds[index] == fd)
        {
            procFreeIOBlock (index);
            return OK;
        }
    }

    return ERROR;
}

/*  ... register STDIN for "radProcessWait" inclusion;
    ... 'ioCallback' will be executed if data or an error occurs on STDIN;
    ... 'userData will be passed to 'ioCallback';
    ... returns PROC_IO_ID or NULL
*/
PROC_IO_ID radProcessIORegisterSTDIN
    (
        void        (*ioCallback) (int fd, void *userData),
        void        *userData
    )
{
    int         i, retVal;

    for (i = PROC_FD_USER_FIRST; i <= PROC_FD_USER_LAST; i ++)
    {
        if (procData.fds[i] != -1)
        {
            continue;
        }

        retVal = procAllocIOBlock (i, STDIN_FILENO, ioCallback, userData);
        if (retVal != OK)
        {
            return ERROR;
        }
        else
        {
            return i;
        }
    }

    return ERROR;
}



/*  *** process signal utilities ***
*/
/*  ... initialize ALL signals to be caught by 'defaultHandler';
    ... returns OK or ERROR
*/
int radProcessSignalCatchAll (void (*defaultHandler) (int signum))
{
    struct sigaction    sigact;

    memset (&sigact, 0, sizeof (sigact));

    sigact.sa_handler   = defaultHandler;

    if (sigaction (SIGHUP, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGINT, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGQUIT, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGILL, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGTRAP, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGABRT, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGBUS, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGFPE, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGSEGV, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGPIPE, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGALRM, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGTERM, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGCHLD, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGTSTP, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGTTIN, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGTTOU, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGURG, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGXCPU, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGXFSZ, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGVTALRM, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGSYS, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGUSR1, &sigact, NULL) != 0)
    {
        return ERROR;
    }
    if (sigaction (SIGUSR2, &sigact, NULL) != 0)
    {
        return ERROR;
    }

    return OK;
}

/*  ... assign a specific 'handler' to a specific 'signal';
    ... returns OK or ERROR
*/
int radProcessSignalCatch (int signum, void (*handler) (int sig))
{
    struct sigaction    sigact;

    memset (&sigact, 0, sizeof (sigact));

    sigact.sa_handler   = handler;

    if (sigaction (signum, &sigact, NULL) != 0)
    {
        return ERROR;
    }

    return OK;
}


/*  ... set 'signal' back to the default system handler;
    ... returns OK or ERROR
*/
int radProcessSignalRelease (int signum)
{
    struct sigaction    sigact;

    memset (&sigact, 0, sizeof (sigact));

    sigact.sa_handler   = SIG_DFL;

    if (sigaction (signum, &sigact, NULL) != 0)
    {
        return ERROR;
    }

    return OK;
}

/*  ... set 'signal' to be ignored;
    ... returns OK or ERROR
*/
int radProcessSignalIgnore (int signum)
{
    struct sigaction    sigact;

    memset (&sigact, 0, sizeof (sigact));

    sigact.sa_handler   = SIG_IGN;

    if (sigaction (signum, &sigact, NULL) != 0)
    {
        return ERROR;
    }

    return OK;
}

/*  ... retrieve the current handler for 'signum';
    ... returns the function pointer or NULL
*/
void (*(radProcessSignalGetHandler (int signum))) (int)
{
    struct sigaction    sigact;

    memset (&sigact, 0, sizeof (sigact));

    if (sigaction (signum, NULL, &sigact) != 0)
    {
        return NULL;
    }

    return (sigact.sa_handler);
}


/*  ... queue wrappers
*/
/*  ... return my queue's name
*/
char *radProcessQueueGetName
(
    char        *store
)
{
    return (radQueueGetName (procData.myQueue, store));
}

/*  ... return my queue's ID
*/
T_QUEUE_ID radProcessQueueGetID
(
    void
)
{
    return procData.myQueue;
}

/*  ... attach to an individual queue based on queue name
    ... so that messages can be sent to it
    ... returns OK or ERROR
*/
int radProcessQueueAttach
(
    char        *newName,
    int         group
)
{
    return (radQueueAttach (procData.myQueue, newName, group));
}

/*  ... dettach from an individual queue based on queue name
*/
int radProcessQueueDettach
(
    char        *oldName,
    int         group
)
{
    return (radQueueDettach (procData.myQueue, oldName, group));
}

/*  ... add my queue to a group and add the group to my address list
    ... returns OK or ERROR
*/
int radProcessQueueJoinGroup
(
    int         groupNumber
)
{
    return (radQueueJoinGroup (procData.myQueue, groupNumber));
}

/*  ... remove my queue from a group and remove a group from my address list
    ... returns OK or ERROR
*/
int radProcessQueueQuitGroup
(
    int         groupNumber
)
{
    return (radQueueQuitGroup (procData.myQueue, groupNumber));
}

/*  ... write to a queue;
    ... assumes sysBuffer is a valid pointer to a system buffer;
    ... system buffer ownership is transfered to the receiving queue;
    ... returns OK, ERROR, or ERROR_ABORT if the dest queue is gone;
    ... user should dettach from a dest on ERROR_ABORT!
*/
int radProcessQueueSend
(
    char        *destQueueName,
    UINT        msgType,
    void        *sysBuffer,
    UINT        length
)
{
    return (radQueueSend (procData.myQueue, destQueueName,
                          msgType, sysBuffer, length));
}

/*  ... write to all queues in a group;
    ... checks to make sure the group hasn't changed - if it has
    ... it refreshes the address list;
    ... assumes sysBuffer is a valid pointer to a system buffer;
    ... system buffer is released if this call returns OK;
    ... returns OK or ERROR
*/
int radProcessQueueSendGroup
(
    int         destGroup,
    UINT        msgType,
    void        *sysBuffer,
    UINT        length
)
{
    return (radQueueSendGroup (procData.myQueue, destGroup,
                               msgType, sysBuffer, length));
}

/*  ... determine if calling process is attached to the given queue name
    ... returns TRUE or FALSE
*/
int radProcessQueueIsAttached
(
    char        *queueName
)
{
    return (radQueueIsAttached (procData.myQueue, queueName));
}

/*  ... ONLY called from inside a message queue handler to indicate that 
    ... retention of the buffer is desired; if not called, the buffer will be
    ... released as usual after the last handler in the traversal list has 
    ... been invoked; this replaces the cheesy method used before to retain
    ... buffers by setting the first byte of the 'srcQueueName' to '0';
    ... It is the caller's responsibility to free the buffer after making this
    ... call to retain it
    ... Note: this will implicitly set the 'radProcessQueueStopHandlerList' 
    ...       condition as well
*/
void radProcessQueueKeepBuffer
(
    void
)
{
    procData.keepMsgQBuffer = TRUE;
    procData.stopMsqQHandlerTraversal = TRUE;
}

/*  ... if more than one message queue handler has been defined via the 
    ... 'radProcessQueuePrependHandler', then this function is used to indicate
    ... that the traversal of the handler list should stop when the calling 
    ... handler returns (i.e., the calling handler "matches" the message 
    ... received)
*/
int radProcessQueueStopHandlerList
(
    void
)
{
    procData.stopMsqQHandlerTraversal = TRUE;
}

/*  ... prepend an additional message queue handler to the existing list of 
    ... message handlers; this allows other utilities/objects/etc. to insert
    ... a message handler to process specific utility messages without the 
    ... radlib application process having to have knowledge of those messages;
    ... this will not interrupt normal application message reception (although
    ... the utility/object doing the insertion should ensure that message types
    ... it is using do not conflict with the application message type 
    ... definitions); this is completely backward compatible with existing 
    ... radlib applications
    ... returns a unique identifier > 0 that can be used to remove the message 
    ... handler later (see 'radProcessQueueRemoveHandler') or ERROR
*/
long radProcessQueuePrependHandler
(
    void            (*msgHandler) (char *srcQueueName,
                                   UINT msgType,
                                   void *msg,
                                   UINT length,
                                   void *userData),
    void            *userdata
)
{
    PROC_MSGQ_HANDLER   *qhandler;

    qhandler = (PROC_MSGQ_HANDLER *) malloc (sizeof (*qhandler));
    if (qhandler == NULL)
    {
        return (long)ERROR;
    }

    qhandler->msgHandler    = msgHandler;
    qhandler->udata         = userdata;
    qhandler->id            = (long)qhandler;
    
    radListAddToFront (&procData.msgqHandlerList, (NODE_PTR)qhandler);
    return (long)qhandler;
}

/*  ... remove a message queue handler from the existing list of message 
    ... handlers; 'handlerID must be a valid return value from a previous
    ... call to 'radProcessQueuePrependHandler' (this implies the default 
    ... handler provided in 'radProcessInit' CANNOT be removed)
    ... returns OK or ERROR
*/
long radProcessQueueRemoveHandler
(
    long                handlerID
)
{
    PROC_MSGQ_HANDLER   *node;

    for (node = (PROC_MSGQ_HANDLER *)radListGetFirst (&procData.msgqHandlerList);
         node != NULL;
         node = (PROC_MSGQ_HANDLER *)radListGetNext (&procData.msgqHandlerList,
                                                     (NODE_PTR)node))
    {
        if (node->id == handlerID)
        {
            radListRemove (&procData.msgqHandlerList, (NODE_PTR)node);
            return (long)OK;
        }
    }

    return (long)ERROR;
}

/*  ... timer wrappers
*/
TIMER_ID radProcessTimerCreate
(
    TIMER_ID  timer,
    void   (*routine) (void *parm),
    void   *parm
)
{
    return (radTimerCreate (timer, routine, parm));
}

void radProcessTimerDelete
(
    TIMER_ID  timer
)
{
    radTimerDelete (timer);
    return;
}

void radProcessTimerStart
(
    TIMER_ID  timer,
    ULONG     time
)
{
    radTimerStart (timer, time);
    return;
}

void radProcessTimerStop
(
    TIMER_ID  timer
)
{
    radTimerStop (timer);
    return;
}

int radProcessTimerStatus
(
    TIMER_ID  timer
)
{
    return (radTimerStatus (timer));
}

void radProcessTimerSetUserParm
(
    TIMER_ID  timer,
    void      *newParm
)
{
    radTimerSetUserParm (timer, newParm);
    return;
}


/*  ... event wrappers
*/
/*  ... add new events to catch
    ... returns OK or ERROR
*/
int radProcessEventsAdd
(
    UINT            newEvents
)
{
    return (radEventsAdd (procData.events, newEvents));
}

/*  ... remove events from event mask
    ... returns OK or ERROR
*/
int radProcessEventsRemove
(
    UINT            removeEvents
)
{
    return (radEventsRemove (procData.events, removeEvents));
}

/*  ... return the currently enabled events mask
*/
UINT radProcessEventsGetEnabled
(
    void
)
{
    return (radEventsGetMask (procData.events));
}

/*  ... send an event to another process by queue name;
    ... returns OK or ERROR
*/
int radProcessEventsSend
(
    char            *destName,
    UINT            eventsToSend,
    UINT            data
)
{
    return (radEventsSend (procData.events, destName, eventsToSend, data));
}

