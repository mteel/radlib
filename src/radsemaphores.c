/*---------------------------------------------------------------------------
 
  FILENAME:
        radsemaphores.c
 
  PURPOSE:
        Provide the semaphore utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        3/21/01         M.S. Teel       0               Original
 
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

#include <stdio.h>
#include <radsemaphores.h>

#include <radsystem.h>



/*  ... local memory
*/
static struct semWorkTag    semWork;



/*  ... called once during process init (returns OK or ERROR)
    ... create a new semaphore, or attach to an existing sem
*/
int radSemProcessInit (void)
{
    if ((semWork.semId = semget (KEY_SEMAPHORES, MAX_SEMAPHORES,
                                 IPC_CREAT | 0644)) == -1)
    {
        semWork.semId = 0;
        return ERROR;
    }

    memset (semWork.status, 0, MAX_SEMAPHORES * sizeof (int));

    return OK;
}

void radSemSetDestroy (void)
{
    union semun     semCtl;
    int             semId;

    if ((semId = semget (KEY_SEMAPHORES, 0, 0644)) == -1)
    {
        return;
    }

    semctl (semId, 0, IPC_RMID, semCtl);

    return;
}

/*  ... create (or attach a specific indexed semaphore
    ... (returns SEM_ID or NULL)
    ... count indicates the initial value of the sem 
    ... (1 for mutex, 0 to attach to existing or initialize locked)
*/
SEM_ID radSemCreate (int semIndex, int count)
{
    SEM_ID      newId;
    union semun arg;

    if (semWork.semId < 0 || semWork.status[semIndex] != 0)
    {
        return NULL;
    }

    /*  ... initialize this guy's count
    */
    if (count >= 0)
    {
        arg.val = count;
        if (semctl (semWork.semId, semIndex, SETVAL, arg) == -1)
        {
            return NULL;
        }
    }

    newId = (SEM_ID) malloc (sizeof (SEMAPHORE));
    if (newId == NULL)
    {
        return NULL;
    }

    newId->semId        = semWork.semId;
    newId->semNumber    = semIndex;

    semWork.status[semIndex] = 1;

    return newId;
}


/*  ... take (lock) a semaphore
*/
void radSemTake (SEM_ID id)
{
    struct sembuf   smBuf = {id->semNumber, -1, 0};

    semop (id->semId, &smBuf, 1);

    return;
}


/*  ... give (unlock) a semaphore
*/
void radSemGive (SEM_ID id)
{
    struct sembuf   smBuf = {id->semNumber, 1, 0};

    semop (id->semId, &smBuf, 1);

    return;
}

/*  ... bump the count on a semaphore
*/
void radSemGiveMultiple (SEM_ID id, int numToGive)
{
    struct sembuf   smBuf = {id->semNumber, numToGive, 0};

    semop (id->semId, &smBuf, 1);

    return;
}

/*  ... test a semaphore to see if it is free (unlocked)
    ... returns TRUE if the semaphore was taken by this call (locked),
    ... FALSE if the semaphore is already locked
*/
int radSemTest (SEM_ID id)
{
    struct sembuf   smBuf = {id->semNumber, 1, IPC_NOWAIT};

    if ((semop (id->semId, &smBuf, 1) == -1) &&
            (errno == EAGAIN))
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}


/*  ... destroy a semaphore
*/
int radSemDelete (SEM_ID id)
{
    semWork.status[id->semNumber] = 0;

    free (id);

    return OK;
}


void radSemDebug (void)
{
    int         i;
    union semun semCtl;
    int         waiters, count, pid, zcount;

    printf ("Semaphore Info:\n");
    printf ("INDEX   COUNT  WAITERS  ZCNT   PID\n");

    for (i = 0; i < MAX_SEMAPHORES; i ++)
    {
        if ((waiters = semctl (semWork.semId, i, GETNCNT, semCtl)) == -1)
        {
            printf ("semctl fail: %s\n", strerror (errno));
            return;
        }

        if ((count = semctl (semWork.semId, i, GETVAL, semCtl)) == -1)
        {
            printf ("semctl fail: %s\n", strerror (errno));
            return;
        }

        if ((pid = semctl (semWork.semId, i, GETPID, semCtl)) == -1)
        {
            printf ("semctl fail: %s\n", strerror (errno));
            return;
        }

        if ((zcount = semctl (semWork.semId, i, GETZCNT, semCtl)) == -1)
        {
            printf ("semctl fail: %s\n", strerror (errno));
            return;
        }

        printf ("%3d     %3d    %3d      %3d     %d\n",
                i, count, waiters, zcount, pid);
    }

    return;
}

