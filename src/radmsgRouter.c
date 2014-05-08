/*---------------------------------------------------------------------------
 
  FILENAME:
        radmsgRouter.c
 
  PURPOSE:
        Provide a standalone message routing process and API to support the 
        "route by message ID" paradigm.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        12/02/2005      M.S. Teel       0               Original
 
  NOTES:
        See radmsgRouter.h for details and usage.

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

//  System include files
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

//  Library include files
#include <radmsgRouter.h>

//  Local include files

//  global memory declarations

//  global memory referenced

//  static (local) memory declarations and methods

static MSGRTR_LOCAL_WORK    msgRtrLocalWork;


// local utilities for the local process API calls
static int waitForRouterAck (void)
{
    char                srcQName[QUEUE_NAME_LENGTH+1];
    UINT                msgType;
    UINT                length;
    void                *recvBfr;
    int                 retVal;
    MSGRTR_HDR          *rtrHdr;
    MSGRTR_INTERNAL_MSG *rtrMsg;
    ULONGLONG           startTime = radTimeGetMSSinceEpoch ();

    // wait for the ACK here
    while (TRUE)
    {
        if ((ULONG)(radTimeGetMSSinceEpoch() - startTime) > MSGRTR_MAX_ACK_WAIT)
        {
            radMsgLog(PRI_STATUS, "waitForRouterAck: ACK timeout");
            return FALSE;
        }

        radUtilsSleep (25);

        if ((retVal = radQueueRecv (radProcessQueueGetID (),
                                    srcQName,
                                    &msgType,
                                    &recvBfr,
                                    &length))
            == FALSE)
        {
            continue;
        }
        else if (retVal == ERROR)
        {
            radMsgLog(PRI_STATUS, "waitForRouterAck: queue is closed!");
            return FALSE;
        }

        // is this an internal router msg?
        if (msgType == MSGRTR_INTERNAL_MSGID)
        {
            // Yes - make sure it is from the router
            rtrHdr = (MSGRTR_HDR *)recvBfr;
            if (rtrHdr->magicNumber != MSGRTR_MAGIC_NUMBER ||
                rtrHdr->msgID != MSGRTR_INTERNAL_MSGID)
            {
                radBufferRls (recvBfr);
                continue;
            }

            rtrMsg = (MSGRTR_INTERNAL_MSG *)rtrHdr->msg;
            if (rtrMsg->subMsgID == MSGRTR_SUBTYPE_ACK)
            {
                // this is what we were wanting...
                radBufferRls (recvBfr);
                return TRUE;
            }
        }
        else
        {
            // just toss the received buffer
            radBufferRls (recvBfr);
        }
    }
}

static int waitForRouterAnswer (void)
{
    char                srcQName[QUEUE_NAME_LENGTH+1];
    UINT                msgType;
    UINT                length;
    void                *recvBfr;
    int                 retVal = ERROR;
    MSGRTR_HDR          *rtrHdr;
    MSGRTR_INTERNAL_MSG *rtrMsg;
    ULONGLONG           startTime = radTimeGetMSSinceEpoch ();

    // wait for the answer here:
    while (TRUE)
    {
        if ((ULONG)(radTimeGetMSSinceEpoch() - startTime) > MSGRTR_MAX_ACK_WAIT)
        {
            radMsgLog(PRI_STATUS, "waitForRouterAnswer: timeout");
            return ERROR;
        }

        radUtilsSleep (25);

        if ((retVal = radQueueRecv (radProcessQueueGetID (),
                                    srcQName,
                                    &msgType,
                                    &recvBfr,
                                    &length))
            == FALSE)
        {
            continue;
        }
        else if (retVal == ERROR)
        {
            radMsgLog(PRI_STATUS, "waitForRouterAnswer: queue is closed!");
            return ERROR;
        }

        // is this an internal router msg?
        if (msgType == MSGRTR_INTERNAL_MSGID)
        {
            // Yes - make sure it is from the router
            rtrHdr = (MSGRTR_HDR *)recvBfr;
            if (rtrHdr->magicNumber != MSGRTR_MAGIC_NUMBER ||
                rtrHdr->msgID != MSGRTR_INTERNAL_MSGID)
            {
                radBufferRls (recvBfr);
                continue;
            }

            rtrMsg = (MSGRTR_INTERNAL_MSG *)rtrHdr->msg;
            if (rtrMsg->subMsgID == MSGRTR_SUBTYPE_MSGID_IS_REGISTERED)
            {
                // this is what we were wanting...
                retVal = rtrMsg->isRegistered;
                radBufferRls (recvBfr);
                return retVal;
            }
        }
        else
        {
            // just toss the received buffer
            radBufferRls (recvBfr);
        }
    }
}

static int sendToRouter (ULONG msgID, void *data, int length)
{
    MSGRTR_HDR          *msg;

    msg = (MSGRTR_HDR *)radBufferGet (sizeof (*msg) + length);
    if (msg == NULL)
    {
        radMsgLog(PRI_HIGH, "sendToRouter: radBufferGet failed!");
        return ERROR;
    }

    msg->magicNumber        = MSGRTR_MAGIC_NUMBER;
    msg->srcpid             = getpid ();
    msg->msgID              = msgID;
    msg->length             = length;
    memcpy (msg->msg, data, length);

    if (radProcessQueueSend (msgRtrLocalWork.rtrQueueName,
                             MSGRTR_INTERNAL_MSGID,
                             msg,
                             sizeof (*msg) + length)
        != OK)
    {
        radMsgLog(PRI_HIGH, "sendToRouter: radProcessQueueSend failed!");
        radBufferRls (msg);
        return ERROR;
    }

    return OK;
}

static int sendPidToRouter (int pid, ULONG msgID, void *data, int length)
{
    MSGRTR_HDR          *msg;

    msg = (MSGRTR_HDR *)radBufferGet (sizeof (*msg) + length);
    if (msg == NULL)
    {
        radMsgLog(PRI_HIGH, "sendToRouter: radBufferGet failed!");
        return ERROR;
    }

    msg->magicNumber        = MSGRTR_MAGIC_NUMBER;
    msg->srcpid             = pid;
    msg->msgID              = msgID;
    msg->length             = length;
    memcpy (msg->msg, data, length);

    if (radProcessQueueSend (msgRtrLocalWork.rtrQueueName,
                             MSGRTR_INTERNAL_MSGID,
                             msg,
                             sizeof (*msg) + length)
        != OK)
    {
        radMsgLog(PRI_HIGH, "sendToRouter: radProcessQueueSend failed!");
        radBufferRls (msg);
        return ERROR;
    }

    return OK;
}


//  API methods

//  Register the message router API for the running process
//  - will instantiate the message router process if it does not exist for the
//  - given 'radlibSystemID'
//  - 'workingDir' specifies where the pid file and FIFOs for the message 
//    router process are to be maintained
//  - returns OK or ERROR
//  Note: 'radSystemInit' and 'radProcessInit' must have been called prior to 
//        calling this function
int radMsgRouterInit (char *workingDir)
{
    struct stat             fileData;
    char                    temp[128];
    MSGRTR_INTERNAL_MSG     rtrMsg;

    sprintf (temp, "%s/%s", workingDir, MSGRTR_LOCK_FILE_NAME);

    // check for the msg router pid file, fail if it isn't there
    if (stat (temp, &fileData) != 0)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterInit: radmrouted not running!");
        return ERROR;
    }

    // set up the local work area
    sprintf (msgRtrLocalWork.rtrQueueName, "%s/%s", workingDir, MSGRTR_QUEUE_NAME);

    //  ... attach to the router's queue
    if (radProcessQueueAttach (msgRtrLocalWork.rtrQueueName, QUEUE_GROUP_ALL) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterInit: radProcessQueueAttach failed!");
        memset (msgRtrLocalWork.rtrQueueName, 0, QUEUE_NAME_LENGTH+1);
        return ERROR;
    }

    // register with the message router
    rtrMsg.subMsgID     = MSGRTR_SUBTYPE_REGISTER;
    strncpy (rtrMsg.name, radProcessGetName(temp), sizeof(rtrMsg.name));
    if (sendToRouter(MSGRTR_INTERNAL_MSGID, &rtrMsg, sizeof(rtrMsg)) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterInit: sendToRouter failed!");
        memset (msgRtrLocalWork.rtrQueueName, 0, QUEUE_NAME_LENGTH);
        return ERROR;
    }

    // wait for the ACK here
    if (waitForRouterAck() != TRUE)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterInit: waitForRouterAck failed!");
        memset (msgRtrLocalWork.rtrQueueName, 0, QUEUE_NAME_LENGTH);
        return ERROR;
    }

    return OK;
}


//  Deregister the message router API for the running process
void radMsgRouterExit (void)
{
    MSGRTR_INTERNAL_MSG     rtrMsg;

    // de-register with the message router
    rtrMsg.subMsgID     = MSGRTR_SUBTYPE_DEREGISTER;

    if (sendToRouter (MSGRTR_INTERNAL_MSGID, &rtrMsg, sizeof(rtrMsg)) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterExit: sendToRouter failed!");
        return;
    }

    radProcessQueueDettach (msgRtrLocalWork.rtrQueueName, QUEUE_GROUP_ALL);
    memset (msgRtrLocalWork.rtrQueueName, 0, QUEUE_NAME_LENGTH+1);

    return;
}


//  Deregister the message router API for the given process id
void radMsgRouterProcessExit (int pid)
{
    MSGRTR_INTERNAL_MSG     rtrMsg;

    // de-register with the message router
    rtrMsg.subMsgID     = MSGRTR_SUBTYPE_DEREGISTER;

    if (sendPidToRouter (pid, MSGRTR_INTERNAL_MSGID, &rtrMsg, sizeof(rtrMsg)) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterExit: sendPidToRouter failed!");
    }

    return;
}


//  Request to receive 'msgID' messages
int radMsgRouterMessageRegister (ULONG msgID)
{
    MSGRTR_INTERNAL_MSG     rtrMsg;

    if (msgRtrLocalWork.rtrQueueName[0] == 0)
    {
        // we have not successfully registered yet
        return ERROR;
    }

    if (msgID == 0)
    {
        // user cannot receive msgID 0
        return ERROR;
    }

    // register with the message router for msgID
    rtrMsg.subMsgID     = MSGRTR_SUBTYPE_ENABLE_MSGID;
    rtrMsg.targetMsgID  = msgID;

    if (sendToRouter(MSGRTR_INTERNAL_MSGID, &rtrMsg, sizeof(rtrMsg)) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterMessageRegister: sendToRouter failed!");
        return ERROR;
    }

    return OK;
}

//  Request if there are any subscribers to 'msgID' messages
int radMsgRouterMessageIsRegistered (ULONG msgID)
{
    MSGRTR_INTERNAL_MSG     rtrMsg;
    int                     retVal = FALSE;

    if (msgID == 0)
    {
        // user cannot register msgID 0
        return FALSE;
    }

    // ask if there are registrants for msgID
    rtrMsg.subMsgID     = MSGRTR_SUBTYPE_MSGID_IS_REGISTERED;
    rtrMsg.targetMsgID  = msgID;

    if (sendToRouter(MSGRTR_INTERNAL_MSGID, &rtrMsg, sizeof(rtrMsg)) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterMessageIsRegistered: sendToRouter failed!");
        return FALSE;
    }

    // wait for the answer here
    retVal = waitForRouterAnswer();
    if (retVal == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterMessageIsRegistered: waitForRouterAnswer failed!");
        retVal = FALSE;
    }

    return retVal;
}

//  Request to NOT receive 'msgID' messages
int radMsgRouterMessageDeregister (ULONG msgID)
{
    MSGRTR_INTERNAL_MSG     rtrMsg;

    if (msgRtrLocalWork.rtrQueueName[0] == 0)
    {
        // we have not successfully registered yet
        return ERROR;
    }

    if (msgID == 0)
    {
        // user cannot receive msgID 0
        return ERROR;
    }

    // deregister with the message router for msgID
    rtrMsg.subMsgID     = MSGRTR_SUBTYPE_DISABLE_MSGID;
    rtrMsg.targetMsgID  = msgID;

    if (sendToRouter(MSGRTR_INTERNAL_MSGID, &rtrMsg, sizeof(rtrMsg)) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterMessageDeregister: sendToRouter failed!");
        return ERROR;
    }

    return OK;
}


//  Send a message through the message router
//  'msg' will be copied into a system buffer - ownership of 'msg' is NOT
//  transferred but remains with the caller
int radMsgRouterMessageSend (ULONG msgID, void *msg, ULONG length)
{
    if (msgRtrLocalWork.rtrQueueName[0] == 0)
    {
        // we have not successfully registered yet
        return ERROR;
    }

    if (msgID == 0)
    {
        // user cannot send msgID 0
        return ERROR;
    }

    radthreadLock();

    if (sendToRouter(msgID, msg, length) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterMessageSend: sendToRouter failed!");
        radthreadUnlock();
        return ERROR;
    }

    radthreadUnlock();
    return OK;
}

//  instruct the message router to dump statistics to the log file
int radMsgRouterStatsDump (void)
{
    MSGRTR_INTERNAL_MSG     rtrMsg;

    if (msgRtrLocalWork.rtrQueueName[0] == 0)
    {
        // we have not successfully registered yet
        return ERROR;
    }

    // dump stats
    rtrMsg.subMsgID     = MSGRTR_SUBTYPE_DUMP_STATS;

    if (sendToRouter(MSGRTR_INTERNAL_MSGID, &rtrMsg, sizeof(rtrMsg)) == ERROR)
    {
        radMsgLog(PRI_HIGH, "radMsgRouterStatsDump: sendToRouter failed!");
        return ERROR;
    }

    return OK;
}

