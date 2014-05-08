/*---------------------------------------------------------------------------
 
  FILENAME:
        radproclist.c
 
  PURPOSE:
        Provide process list utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        04/01/03        M.S. Teel       0               Original
 
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

/*  ... System include files
*/

/*  ... Library include files
*/

/*  ... Local include files
*/
#include <radproclist.h>


/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/
static int insertAscending
(
    PROC_LIST_ID    plistId,
    int             (*entry) (void *pargs),
    void            *args,
    int             priority,
    pid_t           pid,
    int             startNow
)
{
    PROC_DATA_ID    newNode, node, insertAfter = NULL;

    newNode = (PROC_DATA_ID) malloc (sizeof (*newNode));
    if (newNode == NULL)
    {
        radMsgLog(PRI_HIGH, "%s: memory alloc error!", plistId->pName);
        return ERROR;
    }

    memset (newNode, 0, sizeof (*newNode));

    newNode->priority   = priority;
    newNode->entry      = entry;
    newNode->args       = args;
    newNode->pid        = pid;

    /*  ... insert in ascending order
    */
    for (node = (PROC_DATA_ID) radListGetFirst (plistId->procList);
         node != NULL;
         node = (PROC_DATA_ID) radListGetNext (plistId->procList, (NODE_PTR)node))
    {
        if (node->priority > priority)
        {
            break;
        }

        insertAfter = node;
    }

    if (insertAfter == NULL)
    {
        radListAddToFront (plistId->procList, (NODE_PTR)newNode);
    }
    else
    {
        radListInsertAfter (plistId->procList,
                            (NODE_PTR)insertAfter, (NODE_PTR)newNode);
    }


    if (startNow)
    {
        newNode->pid = radStartProcess (newNode->entry, newNode->args);
    }

    return OK;
}


/* ... methods
*/

/*  ... radProcListCreate
    ... create a new, empty process list;
    ... parentName - name used in all radMsgLogs;
    ... returns - PROC_LIST_ID or NULL
*/
PROC_LIST_ID radPlistCreate
(
    char            *parentName
)
{
    PROC_LIST_ID    newId;

    newId = (PROC_LIST_ID) malloc (sizeof (*newId));
    if (newId == NULL)
    {
        radMsgLog(PRI_HIGH, "%s: memory alloc error!", parentName);
        return NULL;
    }

    strncpy (newId->pName, parentName, PROC_PARENT_NAME_LEN-1);
    newId->hasStarted = FALSE;

    newId->semId = radSemCreate (SEM_INDEX_PROCLIST, 0);
    if (newId->semId == NULL)
    {
        radMsgLog(PRI_HIGH, "%s: semaphore create error!", newId->pName);
        free (newId);
        return NULL;
    }

    newId->procList = radListCreate ();
    if (newId->procList == NULL)
    {
        radMsgLog(PRI_HIGH, "%s: list create error!", newId->pName);
        radSemDelete (newId->semId);
        free (newId);
        return NULL;
    }

    radListReset (newId->procList);

    return newId;
}


/*  ... radPlistDestroy
    ... destroy an existing process list;
    ... plistId - the PROC_LIST_ID;
    ... returns - OK or ERROR
*/
int radPlistDestroy
(
    PROC_LIST_ID    plistId
)
{
    NODE_PTR        node;

    radSemDelete (plistId->semId);

    for (node = radListRemoveFirst (plistId->procList);
         node != NULL;
         node = radListRemoveFirst (plistId->procList))
    {
        free (node);
    }

    radListDelete (plistId->procList);
    free (plistId);

    return OK;
}


/*  ... radPlistAdd
    ... add a new "entry" into an existing process list;
    ... plistId - the PROC_LIST_ID;
    ... entry - the entry point for the process;
    ... args - the process-specific args to pass to <entry>;
    ... priority - a value 1 - 100 indicating start priority
    ...            (1 is first, 100 is last);
    ... returns - OK or ERROR
*/
int radPlistAdd
(
    PROC_LIST_ID    plistId,
    int             (*entry) (void *pargs),
    void            *args,
    int             priority
)
{
    if (priority < 1 || priority > 100)
    {
        radMsgLog(PRI_MEDIUM, "%s: process priority out of range!",
                   plistId->pName);
        return ERROR;
    }

    return (insertAscending (plistId, entry, args, priority, 0, FALSE));
}


/*  ... radPlistStart
    ... start all process entries in an existing process list, ordered
    ... by priority (1 -> 100);
    ... plistId - the PROC_LIST_ID;
    ... returns - OK or ERROR
*/
int radPlistStart
(
    PROC_LIST_ID    plistId
)
{
    PROC_DATA_ID    node;

    /*  ... test/set this flag in case of reentrancy
    */
    if (plistId->hasStarted)
    {
        radMsgLog(PRI_HIGH, "%s: process list already started...",
                   plistId->pName);
        return ERROR;
    }
    plistId->hasStarted = TRUE;


    /*  ... start in order
    */
    radMsgLog(PRI_STATUS, "%s: Starting Process List ...", plistId->pName);

    for (node = (PROC_DATA_ID) radListGetFirst (plistId->procList);
         node != NULL;
         node = (PROC_DATA_ID) radListGetNext (plistId->procList, (NODE_PTR)node))
    {
        /*      ... start the process ...
        */
        node->pid = radStartProcess (node->entry, node->args);

        /*      ... then wait for the process to signal he's ready
        */
        radSemTake (plistId->semId);
    }

    radMsgLog(PRI_STATUS, "%s: ... Process List Started", plistId->pName);

    return OK;
}


/*  ... radPlistGetNumberRunning
    ... get the number of running processes (after radPlistStart is called);
    ... plistId - the PROC_LIST_ID;
    ... returns - number of processes successfully started
*/
int radPlistGetNumberRunning
(
    PROC_LIST_ID    plistId
)
{
    PROC_DATA_ID    node;
    int             count = 0;

    for (node = (PROC_DATA_ID) radListGetFirst (plistId->procList);
         node != NULL;
         node = (PROC_DATA_ID) radListGetNext (plistId->procList, (NODE_PTR)node))
    {
        if (node->pid > 0)
        {
            count ++;
        }
    }

    return count;
}


/*  ... radPlistExecByEntryPoint
    ... execute a user-supplied function for the process specified
    ... by <entry> (after radPlistStart is called);
    ... plistId - the PROC_LIST_ID;
    ... entry - the entry point of the process to target;
    ... execFunction - function to call with each process pid and
    ... the supplied void pointer (work area);
    ... data - void pointer to user-supplied data, passed to
    ... "execFunction";
    ... returns - OK or ERROR
*/
int radPlistExecByEntryPoint
(
    PROC_LIST_ID    plistId,
    int             (*entryAdrs) (void *pargs),
    void            (*execFunction) (pid_t pid, void *data),
    void            *data
)
{
    PROC_DATA_ID    node;

    for (node = (PROC_DATA_ID) radListGetFirst (plistId->procList);
         node != NULL;
         node = (PROC_DATA_ID) radListGetNext (plistId->procList, (NODE_PTR)node))
    {
        if (node->entry == entryAdrs)
        {
            (*execFunction) (node->pid, data);
            return OK;
        }
    }

    return ERROR;
}


/*  ... radPlistExecAll
    ... execute a user-supplied function for each running process
    ... (after radPlistStart is called);
    ... plistId - the PROC_LIST_ID;
    ... execFunction - function to call with each process pid and
    ... the supplied void pointer (work area);
    ... data - void pointer to user-supplied data, passed to 
    ... "execFunction";
    ... returns - number of affected processes or ERROR
*/
int radPlistExecAll
(
    PROC_LIST_ID    plistId,
    void            (*execFunction) (pid_t pid, void *data),
    void            *data
)
{
    PROC_DATA_ID    node;
    int             count = 0;

    for (node = (PROC_DATA_ID) radListGetFirst (plistId->procList);
         node != NULL;
         node = (PROC_DATA_ID) radListGetNext (plistId->procList, (NODE_PTR)node))
    {
        if (node->pid > 0)
        {
            (*execFunction) (node->pid, data);
            count ++;
        }
    }

    return count;
}


/*  ... radPlistAddPid
    ... add a process entry to the list;
    ... this may be done when a process is started after
    ... "radPlistStart" has been called to keep the list up to date;
    ... plistId - the PROC_LIST_ID;
    ... pid - pid of process to add to list;
    ... returns - OK or ERROR
*/
int radPlistAddPid
(
    PROC_LIST_ID    plistId,
    pid_t           pid
)
{
    return (insertAscending (plistId, NULL, NULL, 101, pid, FALSE));
}


/*  ... radPlistRemovePid
    ... remove a process entry from the list;
    ... this may be done when it exits to keep the list up to date;
    ... plistId - the PROC_LIST_ID;
    ... pid - pid of process to remove from list;
    ... returns - OK or ERROR if not found
*/
int radPlistRemovePid
(
    PROC_LIST_ID    plistId,
    pid_t           pid
)
{
    PROC_DATA_ID    node;

    for (node = (PROC_DATA_ID) radListGetFirst (plistId->procList);
         node != NULL;
         node = (PROC_DATA_ID) radListGetNext (plistId->procList, (NODE_PTR)node))
    {
        if (node->pid == pid)
        {
            radListRemove (plistId->procList, (NODE_PTR)node);
            free (node);
            return OK;
        }
    }

    /*  ... if here, we didn't find 'em
    */
    return ERROR;
}


/*  ... radPlistAddandStart
    ... add a process entry to the list and start it;
    ... this may be done when a process is started after
    ... "radPlistStart" has been called to keep the list up to date;
    ... plistId - the PROC_LIST_ID;
    ... entry - the entry point for the process;
    ... args - the process-specific args to pass to <entry>;
    ... returns - OK or ERROR;
*/
int radPlistAddandStart
(
    PROC_LIST_ID    plistId,
    int             (*entry) (void *pargs),
    void            *args
)
{
    int             retVal;

    retVal = insertAscending (plistId, entry, args, 101, 0, TRUE);

    /*      ... then wait for the process to signal he's ready
    */
    radSemTake (plistId->semId);

    return retVal;
}


/*  ... radPlistProcessReady
    ... signal to the parent process that initialization is complete
    ... and it is safe to start the next process;
    ... Note: this cannot be called until "radProcessInit" has been 
    ...       successfully called by the new process.
    ... returns - OK or ERROR
*/
int radPlistProcessReady
(
    void
)
{
    SEM_ID      semId;

    semId = radSemCreate (SEM_INDEX_PROCLIST, -1);
    if (semId == NULL)
    {
        return ERROR;
    }

    radSemGive (semId);

    radSemDelete (semId);
    return OK;
}


/*  ... radPlistFindByEntryPoint
    ... find an entry based on its entry point;
    ... returns - the process pid if found or ERROR if not
*/
int radPlistFindByEntryPoint
(
    PROC_LIST_ID    plistId,
    int             (*entryAdrs) (void *pargs)
)
{
    PROC_DATA_ID    node;

    for (node = (PROC_DATA_ID) radListGetFirst (plistId->procList);
         node != NULL;
         node = (PROC_DATA_ID) radListGetNext (plistId->procList, (NODE_PTR)node))
    {
        if (node->entry == entryAdrs)
        {
            return node->pid;
        }
    }

    /*  ... if here, we didn't find 'em
    */
    return ERROR;
}

