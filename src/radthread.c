/*---------------------------------------------------------------------------
 
  FILENAME:
        radthread.c
 
  PURPOSE:
        Provide an easy to use pthread utility.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        03/26/2011      MST             0               Original
 
  NOTES:
        See the header file.
 
  LICENSE:
        Copyright 2001-2011 Mark S. Teel. All rights reserved.

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
#include <stdlib.h>
#include <stdio.h>

/*  ... Local header files
*/
#include <radsysdefs.h>
#include "radthread.h"

// Synchronization for all process threads:
pthread_mutex_t     threadMutex = PTHREAD_MUTEX_INITIALIZER;


// Local methods:
static void* ThreadStub(void* args)
{
    RAD_THREAD_ARGS*    pArgs = (RAD_THREAD_ARGS*)args;

    // Call the user entry point:
    (*(pArgs->Entry))(pArgs->id, pArgs->data);

    // Delete the args so parent doesn't have to:
    free(pArgs);

    return 0;
}


// API methods:

// To create and start a thread:
RAD_THREAD_ID radthreadCreate
(
    void    (*ThreadEntry)(RAD_THREAD_ID threadId, void* threadData), 
    void*   threadData
)
{
    RAD_THREAD_ID   newId;
    pthread_attr_t  Attributes;
    RAD_THREAD_ARGS *args;

    pthread_mutex_lock(&threadMutex);

    newId = malloc(sizeof(struct _radThreadIdTag));
    if (newId == NULL)
    {
        pthread_mutex_unlock(&threadMutex);
        return NULL;
    }

    args = malloc(sizeof(RAD_THREAD_ARGS));
    if (args == NULL)
    {
        free(newId);
        pthread_mutex_unlock(&threadMutex);
        return NULL;
    }

    newId->exitFlag = FALSE;
    radListReset(&newId->ToThreadQueue);
    pthread_cond_init(&newId->ToThreadCondition, NULL);
    pthread_mutex_init(&newId->ToThreadMutex, NULL);
    radListReset(&newId->ToParentQueue);
    pthread_cond_init(&newId->ToParentCondition, NULL);
    pthread_mutex_init(&newId->ToParentMutex, NULL);

    args->Entry = ThreadEntry;
    args->id    = newId;
    args->data  = threadData;

    // Create the new thread:
    pthread_attr_init(&Attributes);
    pthread_attr_setdetachstate(&Attributes, PTHREAD_CREATE_JOINABLE);
    pthread_create(&newId->thread, &Attributes, ThreadStub, args); 

    pthread_mutex_unlock(&threadMutex);
    return newId;
}
        
// To set exit flag and wait for thread to exit:
void radthreadWaitExit(RAD_THREAD_ID threadId)
{
    pthread_mutex_lock(&threadMutex);
    threadId->exitFlag = TRUE;
    pthread_cond_broadcast(&threadId->ToThreadCondition);
    pthread_mutex_unlock(&threadMutex);

    // Wait for him to exit:
    pthread_join(threadId->thread, NULL);

    free(threadId);

    return;
}

// To test for exit command from parent:
int radthreadShouldExit(RAD_THREAD_ID threadId)
{
    int     flag;

    pthread_mutex_lock(&threadMutex);
    flag = threadId->exitFlag;
    pthread_mutex_unlock(&threadMutex);

    return flag;
}

// Parent: To send data to the thread:
int radthreadSendToThread
(
    RAD_THREAD_ID       threadId, 
    int                 type, 
    void*               data, 
    int                 length
)
{
    RAD_THREAD_NODE*    newNode;

    newNode = (RAD_THREAD_NODE*)radBufferGet(sizeof(*newNode) + length);
    if (newNode == NULL)
    {
        return ERROR;
    }
    newNode->type = type;
    newNode->length = length;
    memcpy(newNode->data, data, length);

    pthread_mutex_lock(&threadId->ToThreadMutex);
    radListAddToEnd(&threadId->ToThreadQueue, (NODE_PTR)newNode); 
    pthread_cond_broadcast(&threadId->ToThreadCondition);
    pthread_mutex_unlock(&threadId->ToThreadMutex);

    return OK;
}

// Parent: To receive data from the thread:
int radthreadReceiveFromThread
(
    RAD_THREAD_ID       threadId, 
    void**              data,
    int*                length,
    int                 blocking
)
{
    RAD_THREAD_NODE*    newNode = NULL;
    void*               pretData;
    int                 retVal;

    pthread_mutex_lock(&threadId->ToParentMutex);
    if (radListGetNumberOfNodes(&threadId->ToParentQueue) == 0)
    {
        if (blocking)
        {
            pthread_cond_wait(&threadId->ToParentCondition, 
                              &threadId->ToParentMutex);
        }
        else
        {
            pthread_mutex_unlock(&threadId->ToParentMutex);
            return ERROR_ABORT;
        }
    }

    newNode = (RAD_THREAD_NODE*)radListRemoveFirst(&threadId->ToParentQueue);
    pthread_mutex_unlock(&threadId->ToParentMutex);

    pretData = radBufferGet(newNode->length);
    if (pretData == NULL)
    {
        radBufferRls(newNode);
        return ERROR;
    }

    memcpy(pretData, newNode->data, newNode->length);
    retVal = newNode->type;
    *length = newNode->length;
    *data = pretData;
    radBufferRls(newNode);

    return(retVal);
}

// Thread: To send data to the parent:
int radthreadSendToParent
(
    RAD_THREAD_ID       threadId, 
    int                 type, 
    void*               data, 
    int                 length
)
{
    RAD_THREAD_NODE*    newNode;

    newNode = (RAD_THREAD_NODE*)radBufferGet(sizeof(*newNode) + length);
    if (newNode == NULL)
    {
        return ERROR;
    }
    newNode->type = type;
    newNode->length = length;
    memcpy(newNode->data, data, length);

    pthread_mutex_lock(&threadId->ToParentMutex);
    radListAddToEnd(&threadId->ToParentQueue, (NODE_PTR)newNode); 
    pthread_cond_broadcast(&threadId->ToParentCondition);
    pthread_mutex_unlock(&threadId->ToParentMutex);

    return OK;
}

// Thread: To receive data from the parent:
int radthreadReceiveFromParent
(
    RAD_THREAD_ID   threadId, 
    void**          data, 
    int*            length,
    int             blocking
)
{
    RAD_THREAD_NODE*    newNode = NULL;
    void*               pretData;
    int                 retVal;

    pthread_mutex_lock(&threadId->ToThreadMutex);
    if (radListGetNumberOfNodes(&threadId->ToThreadQueue) == 0)
    {
        if (blocking)
        {
            pthread_cond_wait(&threadId->ToThreadCondition, 
                              &threadId->ToThreadMutex);
        }
        else
        {
            pthread_mutex_unlock(&threadId->ToThreadMutex);
            return ERROR_ABORT;
        }
    }

    newNode = (RAD_THREAD_NODE*)radListRemoveFirst(&threadId->ToThreadQueue);
    pthread_mutex_unlock(&threadId->ToThreadMutex);

    pretData = radBufferGet(newNode->length);
    if (pretData == NULL)
    {
        radBufferRls(newNode);
        return ERROR;
    }

    memcpy(pretData, newNode->data, newNode->length);
    retVal = newNode->type;
    *length = newNode->length;
    *data = pretData;
    radBufferRls(newNode);

    return(retVal);
}

// To lock the thread mutex:
void radthreadLock(void)
{
    pthread_mutex_lock(&threadMutex);
}
 
// To unlock the thread mutex:
void radthreadUnlock(void)
{
    pthread_mutex_unlock(&threadMutex);
}

