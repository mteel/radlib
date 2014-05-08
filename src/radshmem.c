/*---------------------------------------------------------------------------
 
  FILENAME:
        radshmem.c
 
  PURPOSE:
        Provide the shared memory utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        3/23/01         M.S. Teel       0               Original
 
  NOTES:
        Assumes radSemProcessInit has been called for this process.
 
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
#include <string.h>

/*  ... Library include files
*/
#include <radsysdefs.h>
#include <radshmem.h>
#include <raddebug.h>


/*  ... Local include files
*/

/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/


/*  ... body of functions
*/

int radShmemIfExist
(
    int         key
)
{
    int         shmemId;

    /*  ... does the shared memory already exist?
    */
    if ((shmemId = shmget (key, 0, 0664)) != -1)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}


SHMEM_ID radShmemInit
(
    int         key,
    int         semIndex,
    int         size
)
{
    SEM_ID      semId;
    int         shmemId;
    SHMEM_ID    newId;

    /*  ... create the mutual exclusion semaphore
        ... if the shmem already exists, we don't want
        ... to change the count value of the semaphore
        ... thus we pass -1 to radSemCreate
    */
    if (radShmemIfExist (key) == TRUE)
    {
        semId = radSemCreate (semIndex, -1);
    }
    else
    {
        semId = radSemCreate (semIndex, 1);
    }

    if (semId == NULL)
    {
        radMsgLog(PRI_HIGH, "radShmemInit: sem create failed");
        return NULL;
    }

    /*  ... get work area
    */
    if ((newId = (SHMEM_ID) malloc (sizeof (*newId))) == NULL)
    {
        radSemDelete (semId);
        return NULL;
    }

    /*  ... does the shared memory already exist?
    */
    if ((shmemId = shmget (key, 0, 0664)) != -1)
    {
        newId->memory = (SHMEM_ID) shmat (shmemId, 0, 0);
        if (newId->memory == (void *)-1)
        {
            radSemDelete (semId);
            return NULL;
        }

        /*      ... we are good to go!
        */
        newId->semId = semId;
        newId->shmId = shmemId;
        return newId;
    }

    /*  ... we must get our shared memory first
    */
    if ((shmemId = shmget (key, size, IPC_CREAT | 0664)) == -1)
    {
        radSemDelete (semId);
        free (newId);
        return NULL;
    }

    newId->memory = (SHMEM_ID) shmat (shmemId, 0, 0);
    if (newId->memory == (void *)-1)
    {
        radSemDelete (semId);
        free (newId);
        return NULL;
    }


    newId->semId = semId;
    newId->shmId = shmemId;

    return newId;
}

/*  ... get the pointer to the beginning of the shared block
    ... returns the pointer or NULL
*/
void *radShmemGet
(
    SHMEM_ID    id
)
{
    return id->memory;
}

/*  ... lock the block for exclusive access
    ... may block the caller if shared mem is already locked
*/
void radShmemLock
(
    SHMEM_ID    id
)
{
    radSemTake (id->semId);

    return;
}

/*  ... unlock the block
*/
void radShmemUnlock
(
    SHMEM_ID    id
)
{
    radSemGive (id->semId);

    return;
}

/*  ... dettach from a shared block
*/
void radShmemExit
(
    SHMEM_ID    id
)
{
    radSemDelete (id->semId);

    shmdt (id->memory);

    free (id);

    return;
}

/*  ... dettach from a shared block and destroy it
*/
void radShmemExitAndDestroy
(
    SHMEM_ID    id
)
{
    radSemDelete (id->semId);

    shmdt (id->memory);

    shmctl (id->shmId, IPC_RMID, NULL);

    free (id);

    return;
}

