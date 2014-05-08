/*---------------------------------------------------------------------------
 
  FILENAME:
        radbuffers.c
 
  PURPOSE:
        Provide the inter-process message buffer utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        9/26/99         M.S. Teel       0               Original
        3/21/01         M.S. Teel       1               Port to Linux
        02/08/07        M.S. Teel       2               Add 64-bit support
 
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

/*      ... OS include files
*/
#include <stdio.h>
#include <stdlib.h>

/*      ... System include files
*/
#include <radsysdefs.h>
#include <radsystem.h>
#include <radmsgLog.h>
#include <radbuffers.h>

#define _RAD_DBG_ENABLED        0
#include <raddebug.h>

/*  ... Library include files
*/

/*  ... Local include files
*/


/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/
static BUFFER_WKTAG        bufferWork;


/*  ... body of functions
*/

int radBuffersInit
(
    int         minBufferSize,
    int         maxBufferSize,
    int         *numberOfEachSize
)
{
    ULONG       i, j, size, tempInt;
    long        retVal;
    int         numSizes, lastOne = FALSE;
    ULONG       sizes[MAX_BFR_SIZES], offsets[MAX_BFR_SIZES];
    BFR_HDR     *hdr;

    radDEBUGLog ("buffersInit: START");
    
    /*  ... does the shared memory already exist?
    */
    if (radShmemIfExist (KEY_BUFFERS_SHMEM) == TRUE)
    {
        radDEBUGLog ("buffersInit: SHM EXISTS");
        bufferWork.shmId = radShmemInit (KEY_BUFFERS_SHMEM, SEM_INDEX_BUFFERS, 0);
        if (bufferWork.shmId == NULL)
        {
            radMsgLog(PRI_MEDIUM, "radBuffersInit: radShmemInit failed!");
            return ERROR;
        }

        bufferWork.share = (BUFFER_SHARE *) radShmemGet (bufferWork.shmId);
        if (bufferWork.share == NULL)
        {
            radMsgLog(PRI_MEDIUM, "radBuffersInit: radShmemGet failed!");
            return ERROR;
        }

        /*  ... we are good to go!
        */
        return OK;
    }

    /*  ... if just wanting an existing...
    */
    if (maxBufferSize <= 0)
    {
        radMsgLog(PRI_MEDIUM, "radBuffersInit: attach attempt to non-existent segment");
        return ERROR;
    }

    radDEBUGLog ("buffersInit: SHM INIT");
    

    /*  ... we must get our shared memory first - compute the size
    */
    retVal = sizeof (BUFFER_SHARE);

    /*  ... figure out our starting size - we progress by powers of 2
    */
    size = minBufferSize;
    for (tempInt = 0x10; tempInt < size; tempInt <<= 1)
    {
        /*  nothing to do... */
    }

    size = tempInt;

    memset (sizes, 0, MAX_BFR_SIZES * sizeof (ULONG));
    memset (offsets, 0, MAX_BFR_SIZES * sizeof (ULONG));

    for (i = 0;
         i < MAX_BFR_SIZES && numberOfEachSize[i] > 0 && !lastOne;
         i ++)
    {
        if (size >= maxBufferSize)
        {
            /*  we're almost done! */
            lastOne = TRUE;
        }

        /*      ... add the memory needed for this size
        */
        offsets[i] = (size + sizeof (BFR_HDR)) * numberOfEachSize[i];

        retVal += offsets[i];

        /*  now save the size */
        sizes[i] = size;

        /*  all sizes are powers of 2 */
        size <<= 1;
    }

    numSizes = i;
    radDEBUGLog ("buffersInit: SHM SIZE=%d", retVal);


    /*  ... now get the shared memory
    */
    if ((bufferWork.shmId = radShmemInit (KEY_BUFFERS_SHMEM,
                                          SEM_INDEX_BUFFERS, retVal))
        == NULL)
    {
        radMsgLog(PRI_MEDIUM, "radBuffersInit: new radShmemInit failed!");
        return ERROR;
    }

    radDEBUGLog ("buffersInit: SHM GET");
    bufferWork.share = (BUFFER_SHARE *) radShmemGet (bufferWork.shmId);
    if (bufferWork.share == NULL)
    {
        radMsgLog(PRI_MEDIUM, "radBuffersInit: new radShmemGet failed!");
        return ERROR;
    }

    radShmemLock (bufferWork.shmId);

    memset (bufferWork.share, 0, retVal);
    bufferWork.share->numSizes = numSizes;
    bufferWork.share->allocCount = 0;

    tempInt = sizeof (BUFFER_SHARE);

    for (i = 0; sizes[i] != 0 && i < MAX_BFR_SIZES; i ++)
    {
        bufferWork.share->sizes[i] = sizes[i];
        bufferWork.share->count[i] = numberOfEachSize[i];

        /*  ... add all the buffers of this size on the free list
        */
        if (i != 0)
        {
            tempInt += offsets[i-1];
        }

        bufferWork.share->pool[i] = tempInt;

        for (j = 0; j < numberOfEachSize[i]; j ++)
        {
            hdr = (BFR_HDR *)(((UCHAR *)bufferWork.share) +
                              (tempInt + (j * (sizeof (BFR_HDR) + sizes[i]))));

            hdr->sizeIndex = i;
            hdr->allocated = 0;

            if (j == numberOfEachSize[i] - 1)
            {
                hdr->next = 0;
            }
            else
            {
                hdr->next = tempInt + ((j+1) * (sizeof (BFR_HDR) + sizes[i]));
            }
        }
    }


    /*  ... unlock shared memory
    */
    radShmemUnlock (bufferWork.shmId);

    radDEBUGLog ("buffersInit: STOP");
    return OK;
}

/*  ... request a message buffer
    ... returns the pointer or NULL
*/
void *radBufferGet
(
    int         size
)
{
    int         index;
    BFR_HDR     *retPtr;

    /*  ... figure out what size to give him
    */
    radShmemLock (bufferWork.shmId);
    for (index = 0; index < MAX_BFR_SIZES; index ++)
    {
        if (bufferWork.share->sizes[index] >= size)
        {
            break;
        }
    }

    if (index >= MAX_BFR_SIZES)
    {
        /*  user asked for more than we can give */
        radShmemUnlock (bufferWork.shmId);
        return NULL;
    }

    /*  ... get the buffer - if best fit size isn't available try next bigger
    */
    for (/* no init */; index < MAX_BFR_SIZES; index ++)
    {
        if (bufferWork.share->sizes[index] == 0)
        {
            /*  out of partitions - abort! */
            radShmemUnlock (bufferWork.shmId);
            return NULL;
        }

        if (bufferWork.share->pool[index] == 0)
        {
            /*  ... is the buffer pool empty?
            */
            continue;
        }

        retPtr = (BFR_HDR *)((UCHAR *)bufferWork.share + bufferWork.share->pool[index]);
        if (retPtr->allocated != 0)
        {
            if (retPtr->allocated != 1)
            {
                radMsgLog(PRI_HIGH, "radBufferGet: isallocated %d, corrupt", retPtr->allocated);
            }
            continue;
        }

        bufferWork.share->pool[index] = retPtr->next;
        bufferWork.share->allocCount ++;

        radShmemUnlock (bufferWork.shmId);

        /*  bump up the pointer one BFR_HDR to save our header */
        retPtr->allocated = 1;
        retPtr ++;
        return (void *)retPtr;
    }

    /*  we didn't have any! */
    radShmemUnlock (bufferWork.shmId);
    radMsgLog(PRI_MEDIUM, "radBufferGet: failed for size %d", size);
    return NULL;
}


/*  ... release a message buffer
    ... returns OK or ERROR
*/
int radBufferRls
(
    void    *buffer
)
{
    BFR_HDR *ptr = (BFR_HDR *)buffer;

    /*  ... backoff to get to buffer header
    */
    ptr --;

    if (ptr->allocated != 1)
    {
        radMsgLog(PRI_HIGH, 
                   "radBufferRls: trying to release already free buffer or corrupt header!");
        return ERROR;
    }
    else
    {
        ptr->allocated = 0;
    }

    radShmemLock (bufferWork.shmId);

    ptr->next = bufferWork.share->pool[ptr->sizeIndex];
    bufferWork.share->pool[ptr->sizeIndex] = (UCHAR *)ptr - (UCHAR *)bufferWork.share;

    radShmemUnlock (bufferWork.shmId);

    return OK;
}

void *radBufferGetPtr
(
    UINT    offset
)
{
    void    *retPtr;

    retPtr = (void *)((UCHAR *)bufferWork.share + offset);
    return retPtr;
}

UINT radBufferGetOffset
(
    void    *buffer
)
{
    UINT    retVal;

    retVal = (UCHAR *)buffer - (UCHAR *)bufferWork.share;
    return retVal;
}

void radBuffersExit
(
    void
)
{
    radShmemExit (bufferWork.shmId);

    return;
}

void radBuffersExitAndDestroy
(
    void
)
{
    radShmemExitAndDestroy (bufferWork.shmId);

    return;
}

ULONG radBuffersGetTotal
(
    void
)
{
    int             i, sum = 0;

    for (i = 0; i < bufferWork.share->numSizes; i ++)
    {
        sum += bufferWork.share->count[i];
    }

    return sum;
}


static int radBufferGetSizeAvail (int sizeIndex)
{
    BFR_HDR     *hdr;
    int         count = 1;

    if (bufferWork.share->pool[sizeIndex] == 0)
    {
        return 0;
    }

    hdr = (BFR_HDR *)((UCHAR *)bufferWork.share + bufferWork.share->pool[sizeIndex]);

    while (hdr->next != 0)
    {
        count ++;
        hdr = (BFR_HDR *)((UCHAR *)bufferWork.share + hdr->next);
    }

    return count;
}


ULONG radBuffersGetAvailable
(
    void
)
{
    int             i, sum = 0;

    for (i = 0; i < bufferWork.share->numSizes; i ++)
    {
        radShmemLock (bufferWork.shmId);
        sum += radBufferGetSizeAvail (i);
        radShmemUnlock (bufferWork.shmId);
    }

    return sum;
}


void radBuffersDebug (void)
{
    int             i, sum, totalAvail = 0;

    printf ("Buffer Allocation by Size:\n");
    for (i = 0; i < bufferWork.share->numSizes; i ++)
    {
        radShmemLock (bufferWork.shmId);
        sum = radBufferGetSizeAvail (i);
        radShmemUnlock (bufferWork.shmId);
        totalAvail += sum; 

        printf ("Dumping index %d: size %d: ", i, (int)bufferWork.share->sizes[i]);
        printf ("Free/Total %d/%d\n", sum, (int)bufferWork.share->count[i]);
    }

    printf ("\nBuffer Summary:\n\tTotal Free: %d\n\t"
            "Total Allocated: %d\n\tTotal Allocations Since Started: %d\n", 
            (int)radBuffersGetAvailable (),
            (int)radBuffersGetTotal () - (int)radBuffersGetAvailable (),
            bufferWork.share->allocCount);

    return;
}


