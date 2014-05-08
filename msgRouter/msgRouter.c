/*---------------------------------------------------------------------------
 
  FILENAME:
        msgRouter.c
 
  PURPOSE:
        Provide a standalone message routing process to support the 
        "route by message ID" paradigm and the radmsgRouter.h API.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        12/02/2005      M.S. Teel       0               Original
        02/02/2010      MS Teel         1               Add remote routing
 
  NOTES:
        See radmsgRouter.h for API details and usage.

  LICENSE:
        Copyright 2001-2010 Mark S. Teel. All rights reserved.
 
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
#include <sys/socket.h>

//  Library include files
#include <radmsgRouter.h>

//  Local include files

//  global memory declarations

//  global memory referenced

//  static (local) memory declarations:

static MSGRTR_WORK          msgrtrWork;         // for the msg router process only


// Local methods:
static void ClientRXHandler (int fd, void *userData);

//  system initialization:
static int msgrtrSysInit (MSGRTR_WORK *work, char *workingDir)
{
    struct stat     fileData;

    sprintf (work->pidFile, "%s/%s", workingDir, MSGRTR_LOCK_FILE_NAME);
    sprintf (work->fifoFile, "%s/%s", workingDir, MSGRTR_QUEUE_NAME);

    // check for our pid file, don't run if it IS there
    if (stat (work->pidFile, &fileData) == 0)
    {
        radMsgLogInit (PROC_NAME_MSGRTR, TRUE, TRUE);
        radMsgLog(PRI_CATASTROPHIC, 
                   "lock file %s exists, older copy may be running - aborting!",
                   work->pidFile);
        radMsgLogExit ();
        return ERROR;
    }

    return OK;
}

// system exit:
static int msgrtrSysExit (MSGRTR_WORK *work)
{
    struct stat     fileData;

    // delete our pid file:
    if (stat (work->pidFile, &fileData) == 0)
    {
        unlink (work->pidFile);
    }

    return OK;
}

static void defaultSigHandler (int signum)
{
    switch (signum)
    {
        case SIGPIPE:
            signal (signum, defaultSigHandler);
            break;

        case SIGCHLD:
            wait (NULL);
            signal (signum, defaultSigHandler);
            break;

        case SIGILL:
        case SIGBUS:
        case SIGFPE:
        case SIGSEGV:
        case SIGXFSZ:
        case SIGSYS:
            // unrecoverable signal - we must exit right now!
            radMsgLog(PRI_CATASTROPHIC, "%s: recv sig %d: bailing out!", 
                       PROC_NAME_MSGRTR, signum);
            msgrtrSysExit (&msgrtrWork);
            abort ();

        default:
            // can we allow the process to exit normally?
            if (radProcessGetExitFlag())
            {
                // NO! - we gotta bail here!
                radMsgLog(PRI_HIGH, "%s: recv sig %d: exiting now!", 
                           PROC_NAME_MSGRTR, signum);

                msgrtrSysExit (&msgrtrWork);
                exit (0);
            }

            // we can allow the process to exit normally...
            radMsgLog(PRI_HIGH, "%s: recv sig %d: exiting!", 
                       PROC_NAME_MSGRTR, signum);

            radProcessSetExitFlag ();

            signal (signum, defaultSigHandler);
            break;
    }

    return;
}

// Send a message to a remote client:
static int SendToRemote(MSGRTR_PIB* pib, ULONG msgID, void *data, int length)
{
    MSGRTR_HDR          *msg;

    msg = (MSGRTR_HDR *)radBufferGet(sizeof(*msg)+length);
    if (msg == NULL)
    {
        radMsgLog(PRI_HIGH, "SendToRemote: radBufferGet failed!");
        return ERROR;
    }

    msg->magicNumber        = MSGRTR_MAGIC_NUMBER;
    msg->srcpid             = 0;
    msg->msgID              = msgID;
    msg->length             = length;
    memcpy (msg->msg, data, length);

    msg->magicNumber        = htonl(msg->magicNumber);
    msg->srcpid             = htonl(msg->srcpid);
    msg->msgID              = htonl(msg->msgID);
    msg->length             = htonl(msg->length);

    if (radSocketWriteExact(pib->txclient, msg, sizeof(*msg)+length) 
        != sizeof(*msg)+length)
    {
        radMsgLog(PRI_HIGH, "SendToRemote: radSocketWriteExact msg failed!");
        radBufferRls(msg);
        return ERROR;
    }

    radBufferRls(msg);
    return OK;
}

static MSGRTR_PIB *getPIBByPID (int findpid)
{
    MSGRTR_PIB      *node;

    for (node = (MSGRTR_PIB *)radListGetFirst (&msgrtrWork.pibList);
         node != NULL;
         node = (MSGRTR_PIB *)radListGetNext (&msgrtrWork.pibList, (NODE *)node))
    {
        if (node->pid == findpid)
        {
            // got 'em!
            return node;
        }
    }

    return NULL;
}

static int RemovePIB(MSGRTR_PIB* pib)
{
    MSGRTR_PIB      *node;

    for (node = (MSGRTR_PIB *)radListGetFirst (&msgrtrWork.pibList);
         node != NULL;
         node = (MSGRTR_PIB *)radListGetNext (&msgrtrWork.pibList, (NODE *)node))
    {
        if (node == pib)
        {
            // got 'em!
            radListRemove(&msgrtrWork.pibList, (NODE *)node);
            free(pib);
            return OK;
        }
    }

    // didn't find him:
    return ERROR;
}

static MSGRTR_PIB *getPIBBySocket (int socketfd)
{
    MSGRTR_PIB      *node;

    for (node = (MSGRTR_PIB *)radListGetFirst (&msgrtrWork.pibList);
         node != NULL;
         node = (MSGRTR_PIB *)radListGetNext (&msgrtrWork.pibList, (NODE *)node))
    {
        if (node->txclient != NULL)
        {
            if (radSocketGetDescriptor(node->txclient) == socketfd)
            {
                // got 'em!
                return node;
            }
        }
    }

    return NULL;
}

static MSGRTR_MIIB *getMIIB (int findMsgID)
{
    MSGRTR_MIIB     *node;

    for (node = (MSGRTR_MIIB *)radListGetFirst (&msgrtrWork.miibList);
         node != NULL;
         node = (MSGRTR_MIIB *)radListGetNext (&msgrtrWork.miibList, (NODE *)node))
    {
        if (node->msgID == findMsgID)
        {
            // got 'em!
            return node;
        }
    }

    return NULL;
}

// Send an ack to a local client:
static int SendACK (MSGRTR_PIB *dest)
{
    MSGRTR_HDR          *hdr;
    MSGRTR_INTERNAL_MSG *msg;

    hdr = (MSGRTR_HDR *)radBufferGet (sizeof (*hdr) + sizeof(*msg));
    if (hdr == NULL)
    {
        radMsgLog(PRI_HIGH, "SendACK: radBufferGet failed!");
        return ERROR;
    }

    hdr->magicNumber        = MSGRTR_MAGIC_NUMBER;
    hdr->srcpid             = getpid ();
    hdr->msgID              = MSGRTR_INTERNAL_MSGID;
    hdr->length             = sizeof (*msg);

    msg = (MSGRTR_INTERNAL_MSG *)hdr->msg;
    msg->subMsgID           = MSGRTR_SUBTYPE_ACK;

    switch(dest->type)
    {
        case PIB_TYPE_LOCAL:
        {
            if (radProcessQueueSend (dest->queueName,
                                     MSGRTR_INTERNAL_MSGID,
                                     hdr,
                                     sizeof (*hdr) + sizeof(*msg))
                != OK)
            {
                radBufferRls (hdr);
                return ERROR;
            }
            break;
        }
        case PIB_TYPE_REMOTE:
        {
            if (radSocketWriteExact (dest->txclient,
                                     hdr,
                                     sizeof (*hdr) + sizeof(*msg))
                != sizeof (*hdr) + sizeof(*msg))
            {
                radBufferRls (hdr);
                return ERROR;
            }
            radBufferRls(hdr);
            break;
        }
    }
    return OK;
}

// Send a MSGRTR_SUBTYPE_MSGID_IS_REGISTERED answer to a local client:
static int SendIsRegistered (MSGRTR_PIB *dest, int isRegistered)
{
    MSGRTR_HDR          *hdr;
    MSGRTR_INTERNAL_MSG *msg;

    hdr = (MSGRTR_HDR *)radBufferGet (sizeof (*hdr) + sizeof(*msg));
    if (hdr == NULL)
    {
        radMsgLog(PRI_HIGH, "SendACK: radBufferGet failed!");
        return ERROR;
    }

    hdr->magicNumber        = MSGRTR_MAGIC_NUMBER;
    hdr->srcpid             = getpid ();
    hdr->msgID              = MSGRTR_INTERNAL_MSGID;
    hdr->length             = sizeof (*msg);

    msg = (MSGRTR_INTERNAL_MSG *)hdr->msg;
    msg->subMsgID           = MSGRTR_SUBTYPE_MSGID_IS_REGISTERED;
    msg->isRegistered       = isRegistered;

    switch(dest->type)
    {
        case PIB_TYPE_LOCAL:
        {
            if (radProcessQueueSend (dest->queueName,
                                     MSGRTR_INTERNAL_MSGID,
                                     hdr,
                                     sizeof (*hdr) + sizeof(*msg))
                != OK)
            {
                radBufferRls (hdr);
                return ERROR;
            }
            break;
        }
        case PIB_TYPE_REMOTE:
        {
            if (radSocketWriteExact (dest->txclient,
                                     hdr,
                                     sizeof (*hdr) + sizeof(*msg))
                != sizeof (*hdr) + sizeof(*msg))
            {
                radBufferRls (hdr);
                return ERROR;
            }
            radBufferRls(hdr);
            break;
        }
    }
    return OK;
}

// Register one or all existing msgIDs with a remote client:
// Pass 0 for msgID to send all:
static int RegisterRemoteMsgID(MSGRTR_PIB* remote, ULONG msgID, int IsRegister)
{
    MSGRTR_MIIB*        msgNode;
    MSGRTR_INTERNAL_MSG intMsg;

    for (msgNode = (MSGRTR_MIIB*)radListGetFirst(&msgrtrWork.miibList);
         msgNode != NULL;
         msgNode = (MSGRTR_MIIB*)radListGetNext(&msgrtrWork.miibList, (NODE*)msgNode))
    {
        if (msgID == 0 || msgNode->msgID == msgID)
        {
            // Got one:
            memset(&intMsg, 0, sizeof(intMsg));
            if (! IsRegister)
            {
                intMsg.subMsgID     = MSGRTR_SUBTYPE_DISABLE_MSGID;
            }
            else
            {
                intMsg.subMsgID     = MSGRTR_SUBTYPE_ENABLE_MSGID;
            }
            intMsg.targetMsgID  = msgNode->msgID;
            if (SendToRemote(remote, MSGRTR_INTERNAL_MSGID, &intMsg, sizeof(intMsg)) == ERROR)
            {
                radMsgLog(PRI_HIGH, "RegisterRemoteMsgID: SendToRemote failed!");
                return ERROR;
            }
        }
    }

    return OK;
}

// Send msgID registration to all remote clients except "notThisOne":
static int RegisterAllRemotesMsgID(ULONG msgID, MSGRTR_PIB* notThisOne)
{
    MSGRTR_PIB*     pib;

    for (pib = (MSGRTR_PIB*)radListGetFirst(&msgrtrWork.pibList);
         pib != NULL;
         pib = (MSGRTR_PIB*)radListGetNext(&msgrtrWork.pibList, (NODE*)pib))
    {
        if (pib == notThisOne)
        {
            continue;
        }

        if (pib->type == PIB_TYPE_REMOTE)
        {
            RegisterRemoteMsgID(pib, msgID, TRUE);
        }
    }

    return OK;
}

// Send msgID deregistration to all remote clients except "notThisOne":
static int DeregisterAllRemotesMsgID(ULONG msgID, MSGRTR_PIB* notThisOne)
{
    MSGRTR_PIB*     pib;

    for (pib = (MSGRTR_PIB*)radListGetFirst(&msgrtrWork.pibList);
         pib != NULL;
         pib = (MSGRTR_PIB*)radListGetNext(&msgrtrWork.pibList, (NODE*)pib))
    {
        if (pib == notThisOne)
        {
            continue;
        }

        if (pib->type == PIB_TYPE_REMOTE)
        {
            RegisterRemoteMsgID(pib, msgID, FALSE);
        }
    }

    return OK;
}

// Send a message to a client, local or remote:
static int SendToClient(MSGRTR_PIB *consumer, MSGRTR_HDR* hdr)
{
    UCHAR*          sendBfr;
    int             length  = hdr->length;

    switch(consumer->type)
    {
        case PIB_TYPE_LOCAL:
        {
            sendBfr = (UCHAR*)radBufferGet(length);
            if (sendBfr == NULL)
            {
                radMsgLog(PRI_HIGH, "SendToClient: radBufferGet failed!");
                return ERROR;
            }
            memcpy(sendBfr, hdr->msg, length);

            if (radProcessQueueSend (consumer->queueName, hdr->msgID, sendBfr, length)
                != OK)
            {
                radMsgLog(PRI_HIGH, "SendToClient: %s: radProcessQueueSend failed!",
                           consumer->name);
                radBufferRls (sendBfr);
                consumer->rxErrors ++;
                return ERROR;
            }
            break;
        }
        case PIB_TYPE_REMOTE:
        {
            if (consumer->maxMsgSize < length)
            {
                radMsgLog(PRI_HIGH, "SendToClient: %s: msg length %d exceeds remote capability %d!",
                           consumer->name, length, consumer->maxMsgSize);
                consumer->rxErrors ++;
                return ERROR;
            }

            if (SendToRemote(consumer, hdr->msgID, hdr->msg, hdr->length)== ERROR)
            {
                radMsgLog(PRI_HIGH, "SendToClient: %s: SendToRemote failed!",
                           consumer->name);
                consumer->rxErrors ++;
                return ERROR;
            }
            break;
        }
        default:
        {
            radMsgLog(PRI_HIGH, "SendToClient: unknown PIB type %d",
                       consumer->type);
            return ERROR;
        }
    }

    consumer->receives ++;
    msgrtrWork.transmits ++;
    return OK;
}

// Remove a given client from all msgID lists:
static void RemoveClientFromAllMsgs(MSGRTR_PIB *consumer)
{
    MSGRTR_MIIB     *node, *tempnode;
    MSGRTR_CIB      *cib;

    for (node = (MSGRTR_MIIB *)radListGetFirst (&msgrtrWork.miibList);
         node != NULL;
         node = (MSGRTR_MIIB *)radListGetNext (&msgrtrWork.miibList, (NODE *)node))
    {
        // loop through the consumer list for this msgID
        for (cib = (MSGRTR_CIB *)radListGetFirst (&node->consumers);
             cib != NULL;
             cib = (MSGRTR_CIB *)radListGetNext (&node->consumers, (NODE *)cib))
        {
            if (cib->pib == consumer)
            {
                // got 'em!
                radListRemove (&node->consumers, (NODE *)cib);
                free (cib);
                break;
            }
        }

        // if the consumer list is empty, remove the MIIB
        if (radListGetNumberOfNodes(&node->consumers) == 0)
        {
            tempnode = (MSGRTR_MIIB *)radListGetPrevious (&msgrtrWork.miibList, (NODE *)node);
            radListRemove (&msgrtrWork.miibList, (NODE *)node);
            free (node);
            node = tempnode;
        }
    }

    return;
}

// Add a client to a msgID list (check for duplicate):
static void AddClient(MSGRTR_MIIB* miib, MSGRTR_PIB* consumer)
{
    MSGRTR_CIB      *cib;

    // loop through the consumer list for this pib:
    for (cib = (MSGRTR_CIB *)radListGetFirst (&miib->consumers);
         cib != NULL;
         cib = (MSGRTR_CIB *)radListGetNext (&miib->consumers, (NODE *)cib))
    {
        if (cib->pib == consumer)
        {
            // He's already here, just return:
            return;
        }
    }

    // If here, we need to add him:
    cib = (MSGRTR_CIB *)malloc (sizeof(*cib));
    if (cib == NULL)
    {
        radMsgLog(PRI_HIGH, "AddClient: %d: malloc CIB failed!",
                   miib->msgID);
        return;
    }

    cib->pib = consumer;
    radListAddToEnd(&miib->consumers, (NODE *)cib);
    return;
}

// radlib message queue receive handler:
static void QueueMsgHandler
(
    char                *srcQueueName,
    UINT                msgType,
    void                *msg,
    UINT                length,
    void                *userData           // != NULL => socket RX invocation
)
{
    MSGRTR_HDR          *hdr = (MSGRTR_HDR *)msg;
    MSGRTR_INTERNAL_MSG *intMsg = (MSGRTR_INTERNAL_MSG *)hdr->msg;
    MSGRTR_PIB          *sockpib = (MSGRTR_PIB*)userData;
    MSGRTR_PIB          *pib;
    MSGRTR_MIIB         *miib;
    MSGRTR_CIB          *cib;

    // Note: This function is called in two different scenarios:
    // 1) Automatically for internal message RX, in which case sockpib = NULL
    // 2) By the remote socket RX handler, in which case sockpib = RX PIB
    // sockpib is used to determine local or remote reception.
    if (hdr->magicNumber != MSGRTR_MAGIC_NUMBER)
    {
        radMsgLog(PRI_HIGH, "QueueMsgHandler: RX bad magic number 0x%8.8X", 
                   hdr->magicNumber);
        return;
    }

    if (hdr->msgID == MSGRTR_INTERNAL_MSGID)
    {
        switch (intMsg->subMsgID)
        {
            case MSGRTR_SUBTYPE_REGISTER:
            {
                if (sockpib != NULL)
                {
                    // We don't handle socket registers here:
                    return;
                }

                if ((pib = getPIBByPID (hdr->srcpid)) != NULL)
                {
                    // he already exists, just ACK his funky self
                    SendACK (pib);
                    return;
                }

                // let's insert this guy
                pib = (MSGRTR_PIB *)malloc (sizeof(*pib));
                if (pib == NULL)
                {
                    radMsgLog(PRI_HIGH, "QueueMsgHandler: %s: malloc PIB failed!",
                               intMsg->name);
                    return;
                }

                memset (pib, 0, sizeof(*pib));
                pib->type   = PIB_TYPE_LOCAL;
                pib->pid    = hdr->srcpid;
                strncpy (pib->name, intMsg->name, PROCESS_MAX_NAME_LEN);
                strncpy (pib->queueName, srcQueueName, QUEUE_NAME_LENGTH);

                //  attach queue
                if (radProcessQueueAttach (pib->queueName, QUEUE_GROUP_ALL) == ERROR)
                {
                    radMsgLog(PRI_HIGH, "radProcessQueueAttach %s failed!",
                               pib->queueName);
                    free (pib);
                    return;
                }

                radListAddToEnd (&msgrtrWork.pibList, (NODE *)pib);

                // finally, ACK 'em
                SendACK (pib);
                return;
            }

            case MSGRTR_SUBTYPE_DEREGISTER:
            {
                if (sockpib == NULL)
                {
                    if ((pib = getPIBByPID (hdr->srcpid)) == NULL)
                    {
                        // he does not exist
                        return;
                    }
                }
                else
                {
                    pib = sockpib;
                }

                // first, remove him from the consumer lists
                RemoveClientFromAllMsgs (pib);

                // remove him from the PIB list
                radListRemove (&msgrtrWork.pibList, (NODE *)pib);

                if (sockpib == NULL)
                {
                    radProcessQueueDettach(pib->queueName, QUEUE_GROUP_ALL);
                }
                else
                {
                    radSocketDestroy(pib->txclient);
                    radSocketDestroy(pib->rxclient);
                }
                free (pib);

                return;
            }

        case MSGRTR_SUBTYPE_ENABLE_MSGID:
        {
            if (sockpib == NULL)
            {
                if ((pib = getPIBByPID (hdr->srcpid)) == NULL)
                {
                    // he does not exist
                    return;
                }
            }
            else
            {
                pib = sockpib;
            }

            // first, get the MIID
            miib = getMIIB(intMsg->targetMsgID);
            if (miib == NULL)
            {
                // new msgID, create the MIIB
                miib = (MSGRTR_MIIB *)malloc (sizeof(*miib));
                if (miib == NULL)
                {
                    radMsgLog(PRI_HIGH, "QueueMsgHandler: %d: malloc MIIB failed!",
                               intMsg->targetMsgID);
                    return;
                }

                miib->msgID = intMsg->targetMsgID;
                radListReset (&miib->consumers);
                radListAddToEnd (&msgrtrWork.miibList, (NODE *)miib);
            }

            // now that we have the MIIB, add this consumer
            AddClient(miib, pib);

            // Register with all remotes for this msgID too:
            RegisterAllRemotesMsgID(miib->msgID, sockpib);

            return;
        }

        case MSGRTR_SUBTYPE_MSGID_IS_REGISTERED:
        {
            int     isRegistered = 0;

            if (sockpib == NULL)
            {
                if ((pib = getPIBByPID (hdr->srcpid)) == NULL)
                {
                    // he does not exist
                    return;
                }
            }
            else
            {
                pib = sockpib;
            }

            // first, get the MIID
            miib = getMIIB(intMsg->targetMsgID);
            if (miib == NULL)
            {
                // unknown msgId
                return;
            }

            // Are there any consumers?
            if (radListGetNumberOfNodes(&miib->consumers) > 0)
            {
                isRegistered = 1;
            }

            // Send response:
            SendIsRegistered (pib, isRegistered);

            return;
        }

            case MSGRTR_SUBTYPE_DISABLE_MSGID:
            {
                if (sockpib == NULL)
                {
                    if ((pib = getPIBByPID (hdr->srcpid)) == NULL)
                    {
                        // he does not exist
                        return;
                    }
                }
                else
                {
                    pib = sockpib;
                }

                // first, get the MIIB
                miib = getMIIB(intMsg->targetMsgID);
                if (miib == NULL)
                {
                    // msgID does not exist...
                    return;
                }

                // now that we have the MIIB, remove this consumer
                for (cib = (MSGRTR_CIB *)radListGetFirst (&miib->consumers);
                     cib != NULL;
                     cib = (MSGRTR_CIB *)radListGetNext (&miib->consumers, (NODE *)cib))
                {
                    if (cib->pib == pib)
                    {
                        // remove him
                        radListRemove (&miib->consumers, (NODE *)cib);
                        free (cib);
                        break;
                    }
                }

                // Check here to see if there are any more consumers:
                // If not, remove the MIIB and deregister with remote guys:
                if (radListGetNumberOfNodes(&miib->consumers) == 0)
                {
                    // Deregister RX to all remotes (except possibly the sender):
                    DeregisterAllRemotesMsgID(intMsg->targetMsgID, sockpib);

                    // Remove the MIIB:
                    radListRemove(&msgrtrWork.miibList, (NODE *)miib);

                    radMsgLog(PRI_STATUS, "QueueMsgHandler: removed empty MIIB for msgID %d",
                               intMsg->targetMsgID);
                }

                return;
            }

            case MSGRTR_SUBTYPE_DUMP_STATS:
            {
                radMsgLog(PRI_MEDIUM, "---------- Message Router Totals:  TX:%10.10u  RX:%10.10u ----------",
                           msgrtrWork.transmits, msgrtrWork.receives);
                radMsgLog(PRI_MEDIUM, "     Name     \t MSGS TX  \t MSGS RX  \t  TXERRS  \t  RXERRS");
                radMsgLog(PRI_MEDIUM, "--------------\t----------\t----------\t----------\t----------");

                // loop through the PIBs
                for (pib = (MSGRTR_PIB *)radListGetFirst (&msgrtrWork.pibList);
                     pib != NULL;
                     pib = (MSGRTR_PIB *)radListGetNext (&msgrtrWork.pibList, (NODE *)pib))
                {
                    radMsgLog(PRI_MEDIUM, "%-14s\t%10u\t%10u\t%10u\t%10u",
                               pib->name,
                               pib->transmits, 
                               pib->receives,
                               pib->txErrors,
                               pib->rxErrors);
                }

                radMsgLog(PRI_MEDIUM, "--------------------------------------------------------------------------");
                return;
            }
        }

        return;
    }

    
    //// normal message, route it ////

    // get the PIB
    if (sockpib == NULL)
    {
        if ((pib = getPIBByPID (hdr->srcpid)) == NULL)
        {
            // he does not exist
            return;
        }
    }
    else
    {
        pib = sockpib;
    }

    msgrtrWork.receives ++;

    // get the proper MIIB
    miib = getMIIB (hdr->msgID);
    if (miib == NULL)
    {
        // no one to send it to
        pib->txErrors ++;
        return;
    }

    pib->transmits ++;

    // loop through the consumer list, sending it to each one of them
    for (cib = (MSGRTR_CIB *)radListGetFirst (&miib->consumers);
         cib != NULL;
         cib = (MSGRTR_CIB *)radListGetNext (&miib->consumers, (NODE *)cib))
    {
        if (cib->pib == sockpib)
        {
            // Don't send it back to the remote source (we allow local loopback):
            continue;
        }

        // send it to him
        if (SendToClient(cib->pib, hdr) == ERROR)
        {
            radMsgLog(PRI_HIGH, "QueueMsgHandler: %s: SendToClient failed!",
                      cib->pib->name);
        }
    }
            
    return;
}

// Listen socket's connection queue handler:
static void ServerRXHandler (int fd, void *userData)
{
    RADSOCK_ID          newClient;
    RADSOCK_ID          newServer;
    MSGRTR_HDR          msgHdr;
    MSGRTR_INTERNAL_MSG inMsg, outMsg;
    MSGRTR_PIB*         pib;

    newClient = radSocketServerAcceptConnection(msgrtrWork.server);
    if (newClient == NULL)
    {
        radMsgLog(PRI_HIGH, "ServerRXHandler: radSocketServerAcceptConnection failed!");
        return;
    }

    // Wait for the register pkt:
    if (radSocketReadExact(newClient, &msgHdr, sizeof(msgHdr)) != sizeof(msgHdr))
    {
        radMsgLog(PRI_HIGH, "ServerRXHandler: radSocketReadExact HDR failed!");
        radSocketDestroy(newClient);
        return;
    }

    // Do NtoH conversions:
    msgHdr.magicNumber  = ntohl(msgHdr.magicNumber);
    msgHdr.srcpid       = ntohl(msgHdr.srcpid);
    msgHdr.msgID        = ntohl(msgHdr.msgID);
    msgHdr.length       = ntohl(msgHdr.length);

    if (msgHdr.magicNumber != MSGRTR_MAGIC_NUMBER)
    {
        radMsgLog(PRI_HIGH, "ServerRXHandler: HDR magic failed!");
        radSocketDestroy(newClient);
        return;
    }

    if (msgHdr.msgID != MSGRTR_INTERNAL_MSGID)
    {
        radMsgLog(PRI_HIGH, "ServerRXHandler: HDR ID not internal!");
        radSocketDestroy(newClient);
        return;
    }

    // Read the rest:
    if (radSocketReadExact(newClient, &inMsg, sizeof(inMsg)) != sizeof(inMsg))
    {
        radMsgLog(PRI_HIGH, "ServerRXHandler: radSocketReadExact inMsg failed!");
        radSocketDestroy(newClient);
        return;
    }

    // Do NtoH conversions:
    inMsg.subMsgID      = ntohl(inMsg.subMsgID);
    inMsg.targetMsgID   = ntohl(inMsg.targetMsgID);
    inMsg.srcPort       = ntohl(inMsg.srcPort);
    inMsg.socketID      = ntohl(inMsg.socketID);
    inMsg.maxMsgSize    = ntohl(inMsg.maxMsgSize);

    if (inMsg.subMsgID == MSGRTR_SUBTYPE_REGISTER)
    {
        radMsgLog(PRI_STATUS, "Remote accept RX: %s:%d <== %s:%d",
                   radSocketGetHost(newClient), 
                   radSocketGetPort(newClient),
                   radSocketGetRemoteHost(newClient), 
                   radSocketGetRemotePort(newClient));

        // Now open the TX side socket:
        newServer = radSocketClientCreate(inMsg.srcIP, inMsg.srcPort);
        if (newServer == NULL)
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: radSocketClientCreate %s:%d failed!",
                       inMsg.srcIP, inMsg.srcPort);
            radSocketDestroy(newClient);
            return;
        }

        // Send him an ack on this new socket:
        msgHdr.magicNumber      = MSGRTR_MAGIC_NUMBER;
        msgHdr.srcpid           = 0;
        msgHdr.msgID            = MSGRTR_INTERNAL_MSGID;
        msgHdr.length           = sizeof(MSGRTR_INTERNAL_MSG);

        outMsg.subMsgID         = MSGRTR_SUBTYPE_ACK;
        sprintf(outMsg.name, "router:%s", radSocketGetHost(newServer));
        strncpy(outMsg.srcIP, inMsg.srcIP, sizeof(outMsg.srcIP));
        outMsg.srcPort          = inMsg.srcPort;
        outMsg.socketID         = inMsg.socketID;
        outMsg.maxMsgSize       = SYS_BUFFER_LARGEST_SIZE;

        // Do HtoN conversions:
        msgHdr.magicNumber  = htonl(msgHdr.magicNumber);
        msgHdr.srcpid       = htonl(msgHdr.srcpid);
        msgHdr.msgID        = htonl(msgHdr.msgID);
        msgHdr.length       = htonl(msgHdr.length);
        outMsg.subMsgID     = htonl(outMsg.subMsgID);
        outMsg.targetMsgID  = htonl(outMsg.targetMsgID);
        outMsg.srcPort      = htonl(outMsg.srcPort);
        outMsg.socketID     = htonl(outMsg.socketID);
        outMsg.maxMsgSize   = htonl(outMsg.maxMsgSize);

        if (radSocketWriteExact(newServer, &msgHdr, sizeof(msgHdr)) != sizeof(msgHdr))
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: radSocketWriteExact msgHdr failed!");
            radSocketDestroy(newServer);
            radSocketDestroy(newClient);
            return;
        }
        if (radSocketWriteExact(newServer, &outMsg, sizeof(outMsg)) != sizeof(outMsg))
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: radSocketWriteExact outMsg failed!");
            radSocketDestroy(newServer);
            radSocketDestroy(newClient);
            return;
        }

        // OK, we are done, store the sockets in a new PIB:
        pib = (MSGRTR_PIB *)malloc (sizeof(*pib));
        if (pib == NULL)
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: %s: malloc PIB failed!",
                       inMsg.name);
            radSocketDestroy(newServer);
            radSocketDestroy(newClient);
            return;
        }

        memset (pib, 0, sizeof(*pib));
        pib->type       = PIB_TYPE_REMOTE;
        strncpy (pib->name, inMsg.name, PROCESS_MAX_NAME_LEN);
        pib->rxclient   = newClient;
        pib->txclient   = newServer;
        pib->maxMsgSize = inMsg.maxMsgSize;

        // Now register for all messages we are interested in:
        if (RegisterRemoteMsgID(pib, 0, TRUE) == ERROR)
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: RegisterRemoteMsgID failed");
            radSocketDestroy(pib->rxclient);
            radSocketDestroy(pib->txclient);
            free(pib);
            return;
        }

        radListAddToEnd(&msgrtrWork.pibList, (NODE *)pib);

        // Add the RX socket to our wait list:
        radProcessIORegisterDescriptor(radSocketGetDescriptor(pib->rxclient),
                                       ClientRXHandler,
                                       (void*)pib);

        radMsgLog(PRI_STATUS, "Remote Accept: %s:%d ==> %s:%d",
                   radSocketGetHost(pib->txclient), 
                   radSocketGetPort(pib->txclient),
                   radSocketGetRemoteHost(pib->txclient), 
                   radSocketGetRemotePort(pib->txclient));
    }
    else if (inMsg.subMsgID == MSGRTR_SUBTYPE_ACK)
    {
        // Now verify:
        if (strncmp(radSocketGetHost(newClient), inMsg.srcIP, sizeof(inMsg.srcIP)))
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: ACK local IP:%s does not match %s",
                       radSocketGetHost(newClient), inMsg.srcIP);
            radSocketDestroy(newClient);
            return;
        }
        if (msgrtrWork.listenPort != inMsg.srcPort)
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: ACK local port:%d does not match %d",
                       msgrtrWork.listenPort, inMsg.srcPort);
            radSocketDestroy(newClient);
            return;
        }

        // They match, add the RX socket to the PIB:
        pib = getPIBBySocket(radSocketGetDescriptor((RADSOCK_ID)inMsg.socketID));
        if (pib == NULL)
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: ACK getPIBBySocket failed");
            radSocketDestroy(newClient);
            return;
        }
        pib->rxclient   = newClient;
        pib->maxMsgSize = inMsg.maxMsgSize;

        // Now register for all messages we are interested in:
        if (RegisterRemoteMsgID(pib, 0, TRUE) == ERROR)
        {
            radMsgLog(PRI_HIGH, "ServerRXHandler: RegisterRemoteMsgID failed");
            RemovePIB(pib);
            return;
        }

        // Add to wait list:
        radProcessIORegisterDescriptor(radSocketGetDescriptor(pib->rxclient),
                                       ClientRXHandler,
                                       (void*)pib);        

        radMsgLog(PRI_STATUS, "ACK RX from Remote: %s:%d <== %s:%d",
                   radSocketGetHost(pib->rxclient), 
                   radSocketGetPort(pib->rxclient),
                   radSocketGetRemoteHost(pib->rxclient), 
                   radSocketGetRemotePort(pib->rxclient));
    }
    else
    {
        radMsgLog(PRI_HIGH, "ServerRXHandler: unexpected subMsgId %d", 
                   inMsg.subMsgID);
        radSocketDestroy(newClient);
        return;
    }
}

// Client RX message handler:
static UCHAR SocketRXBuffer[sizeof(MSGRTR_HDR) + SYS_BUFFER_LARGEST_SIZE];
static void ClientRXHandler (int fd, void *userData)
{
    MSGRTR_PIB*         pib = (MSGRTR_PIB*)userData;
    MSGRTR_HDR*         msgHdr = (MSGRTR_HDR*)SocketRXBuffer;
    MSGRTR_INTERNAL_MSG inMsg;
    int                 IsRemote = FALSE;

    // Read the header:
    if (radSocketReadExact(pib->rxclient, msgHdr, sizeof(MSGRTR_HDR)) 
        != sizeof(MSGRTR_HDR))
    {
        radMsgLog(PRI_HIGH, "ClientRXHandler: radSocketReadExact HDR failed - closing!");
        RemoveClientFromAllMsgs (pib);

        // remove him from the PIB list
        radListRemove (&msgrtrWork.pibList, (NODE *)pib);

        if (msgrtrWork.remoteServer == pib->txclient)
        {
            IsRemote = TRUE;
        }

        radProcessIODeRegisterDescriptorByFd(radSocketGetDescriptor(pib->rxclient));
        radSocketDestroy(pib->txclient);
        radSocketDestroy(pib->rxclient);
        free (pib);

        // Restart acquisition timer?
        if (IsRemote)
        {
            msgrtrWork.remoteServer = NULL;
            radTimerStart(msgrtrWork.remoteConnectTimer, MSGRTR_REMOTE_RETRY_INTERVAL);
        }
        return;
    }

    // Do NtoH conversions:
    msgHdr->magicNumber  = ntohl(msgHdr->magicNumber);
    msgHdr->srcpid       = ntohl(msgHdr->srcpid);
    msgHdr->msgID        = ntohl(msgHdr->msgID);
    msgHdr->length       = ntohl(msgHdr->length);

    if (msgHdr->magicNumber != MSGRTR_MAGIC_NUMBER)
    {
        radMsgLog(PRI_HIGH, "ClientRXHandler: HDR magic failed - closing!");
        RemoveClientFromAllMsgs (pib);

        // remove him from the PIB list
        radListRemove (&msgrtrWork.pibList, (NODE *)pib);

        radProcessIODeRegisterDescriptorByFd(radSocketGetDescriptor(pib->rxclient));
        radSocketDestroy(pib->txclient);
        radSocketDestroy(pib->rxclient);
        free (pib);
        return;
    }

    // Read the rest:
    if (radSocketReadExact(pib->rxclient, msgHdr->msg, msgHdr->length) 
        != msgHdr->length)
    {
        radMsgLog(PRI_HIGH, "ServerRXHandler: radSocketReadExact payload failed!");
        RemoveClientFromAllMsgs (pib);

        // remove him from the PIB list
        radListRemove (&msgrtrWork.pibList, (NODE *)pib);

        radProcessIODeRegisterDescriptorByFd(radSocketGetDescriptor(pib->rxclient));
        radSocketDestroy(pib->txclient);
        radSocketDestroy(pib->rxclient);
        free (pib);
        return;
    }

    // Pass the pkt to the queue msg handler (he handles socket data too):
    QueueMsgHandler(0, msgHdr->msgID, msgHdr, sizeof(MSGRTR_HDR) + msgHdr->length, pib);
}

// radlib event handler (not used):
static void EventHandler
(
    UINT        eventsRx,
    UINT        rxData,
    void        *userData
)
{
    return;
}

// Remote connection timer handler:
static void connectTimerHandler(void *parm)
{
    MSGRTR_HDR          msgHdr;
    MSGRTR_INTERNAL_MSG outMsg;
    MSGRTR_PIB*         pib;

    radMsgLog(PRI_STATUS, "msgRouter: trying remote server %s:%d...", 
               msgrtrWork.remoteIP, msgrtrWork.remotePort);

    msgrtrWork.remoteServer = radSocketClientCreate(msgrtrWork.remoteIP, msgrtrWork.remotePort);
    if (msgrtrWork.remoteServer == NULL)
    {
        radTimerStart(msgrtrWork.remoteConnectTimer, MSGRTR_REMOTE_RETRY_INTERVAL);
        return;
    }

    // Build the register pkt:
    msgHdr.magicNumber      = MSGRTR_MAGIC_NUMBER;
    msgHdr.srcpid           = 0;
    msgHdr.msgID            = MSGRTR_INTERNAL_MSGID;
    msgHdr.length           = sizeof(MSGRTR_INTERNAL_MSG);

    outMsg.subMsgID         = MSGRTR_SUBTYPE_REGISTER;
    sprintf(outMsg.name, "router:%s", radSocketGetHost(msgrtrWork.remoteServer));
    strncpy(outMsg.srcIP, radSocketGetHost(msgrtrWork.remoteServer), sizeof(outMsg.srcIP));
    outMsg.srcPort          = msgrtrWork.listenPort;
    outMsg.socketID         = (ULONG)msgrtrWork.remoteServer;
    outMsg.maxMsgSize       = SYS_BUFFER_LARGEST_SIZE;

    // Do HtoN conversions:
    msgHdr.magicNumber  = htonl(msgHdr.magicNumber);
    msgHdr.srcpid       = htonl(msgHdr.srcpid);
    msgHdr.msgID        = htonl(msgHdr.msgID);
    msgHdr.length       = htonl(msgHdr.length);
    outMsg.subMsgID     = htonl(outMsg.subMsgID);
    outMsg.targetMsgID  = htonl(outMsg.targetMsgID);
    outMsg.srcPort      = htonl(outMsg.srcPort);
    outMsg.socketID     = htonl(outMsg.socketID);
    outMsg.maxMsgSize   = htonl(outMsg.maxMsgSize);

    if (radSocketWriteExact(msgrtrWork.remoteServer, &msgHdr, sizeof(msgHdr)) != sizeof(msgHdr))
    {
        radMsgLog(PRI_HIGH, "radSocketWriteExact msgHdr failed!");
        radSocketDestroy(msgrtrWork.remoteServer);
        return;
    }
    if (radSocketWriteExact(msgrtrWork.remoteServer, &outMsg, sizeof(outMsg)) != sizeof(outMsg))
    {
        radMsgLog(PRI_HIGH, "radSocketWriteExact outMsg failed!");
        radSocketDestroy(msgrtrWork.remoteServer);
        return;
    }

    // Add him to the PIB list:
    pib = (MSGRTR_PIB *)malloc (sizeof(*pib));
    if (pib == NULL)
    {
        radMsgLog(PRI_HIGH, "%s: malloc PIB failed!", outMsg.name);
        radSocketDestroy(msgrtrWork.remoteServer);
        return;
    }

    memset (pib, 0, sizeof(*pib));
    pib->type       = PIB_TYPE_REMOTE;
    strncpy (pib->name, outMsg.name, PROCESS_MAX_NAME_LEN);
    pib->txclient   = msgrtrWork.remoteServer;
    radListAddToEnd(&msgrtrWork.pibList, (NODE *)pib);

    radMsgLog(PRI_STATUS, "Remote server TX: %s:%d ==> %s:%d",
               radSocketGetHost(msgrtrWork.remoteServer), 
               radSocketGetPort(msgrtrWork.remoteServer),
               radSocketGetRemoteHost(msgrtrWork.remoteServer), 
               radSocketGetRemotePort(msgrtrWork.remoteServer));

    return;
}


static void USAGE (void)
{
    printf ("%s: invalid arguments:\n", PROC_NAME_MSGRTR);
    printf ("USAGE: [prefix]/%s radSystemID workingDirectory <listenPort> <remoteIP:remotePort>\n", PROC_NAME_MSGRTR);
    
    printf ("    radSystemID           1-255, same system ID used by other processes in this group\n");
    printf ("    workingDirectory      where to store FIFO and pid files for radmrouted\n");
    printf ("    listenPort            (optional) socket sever listen port to accept remote connections\n");
    printf ("    remoteIP:remotePort   (optional) remote host IP:port to connect to\n");
}


// the entry point for the message router process
int main (int argc, char *argv[])
{
    void                (*alarmHandler)(int);
    int                 retVal;
    FILE                *pidfile;
    int                 radSysID;
    char                *word;

    // We must have radSysID and working directory as a minimum:
    if (argc < 3)
    {
        USAGE ();
        exit (1);
    }

    radSysID = atoi(argv[1]);
    if (radSysID < 1 || radSysID > 255)
    {
        USAGE ();
        exit (2);
    }

    memset (&msgrtrWork, 0, sizeof (msgrtrWork));
    radListReset (&msgrtrWork.pibList);
    radListReset (&msgrtrWork.miibList);

    // initialize some system stuff first
    retVal = msgrtrSysInit (&msgrtrWork, argv[2]);
    if (retVal == ERROR)
    {
        radMsgLogInit (PROC_NAME_MSGRTR, FALSE, TRUE);
        radMsgLog(PRI_CATASTROPHIC, "init failed!");
        radMsgLogExit ();
        exit (1);
    }

    msgrtrWork.radSystemID = (UCHAR)radSysID;

    // Listening for remote clients?
    if (argc > 3)
    {
        msgrtrWork.listenPort = atoi(argv[3]);
    }

    // Connecting to a remote router?
    if (argc > 4)
    {
        word = strtok(argv[4], ":");
        if (word == NULL)
        {
            radMsgLogInit (PROC_NAME_MSGRTR, FALSE, TRUE);
            radMsgLog(PRI_CATASTROPHIC, "bad remoteIP:remotePort given - ignoring!");
            radMsgLogExit ();
        }
        else
        {
            strncpy(msgrtrWork.remoteIP, word, sizeof(msgrtrWork.remoteIP));
            word = strtok(NULL, ":");
            if (word == NULL)
            {
                msgrtrWork.remoteIP[0] = 0;
                radMsgLogInit (PROC_NAME_MSGRTR, FALSE, TRUE);
                radMsgLog(PRI_CATASTROPHIC, "bad remoteIP:remotePort given - ignoring!");
                radMsgLogExit ();
            }
            else
            {
                msgrtrWork.remotePort = atoi(word);
            }
        }
    }

    /*  ... call the global radlib system init function
    */
    if (radSystemInit (msgrtrWork.radSystemID) == ERROR)
    {
        radMsgLogInit (PROC_NAME_MSGRTR, TRUE, TRUE);
        radMsgLog(PRI_CATASTROPHIC, "radSystemInit failed!");
        radMsgLogExit ();
        exit (1);
    }

    //  ... call the radlib process init function
    // Note: It is mandatory that the userData is passed in as NULL here; it is
    //       used by the message handler to determine local or socket RXs.
    if (radProcessInit (PROC_NAME_MSGRTR,
                        msgrtrWork.fifoFile,
                        MSGRTR_NUM_TIMERS,
                        TRUE,                       // TRUE for daemon
                        QueueMsgHandler,
                        EventHandler,
                        NULL)                       // must be NULL!
        == ERROR)
    {
        printf ("\nradmrouted: radProcessInit failed: %s\n\n", PROC_NAME_MSGRTR);
        radSystemExit (msgrtrWork.radSystemID);
        exit (1);
    }

    msgrtrWork.myPid = getpid ();
    pidfile = fopen (msgrtrWork.pidFile, "w");
    if (pidfile == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "lock file create failed!\n");
        radProcessExit ();
        radSystemExit (msgrtrWork.radSystemID);
        exit (1);
    }
    fprintf (pidfile, "%d", getpid ());
    fclose (pidfile);


    alarmHandler = radProcessSignalGetHandler (SIGALRM);
    radProcessSignalCatchAll (defaultSigHandler);
    radProcessSignalCatch (SIGALRM, alarmHandler);

    radMsgLog(PRI_MEDIUM, "started on radlib system %d, workdir %s",
               msgrtrWork.radSystemID, argv[2]);

    // Create our remote timer:
    msgrtrWork.remoteConnectTimer = radTimerCreate(NULL, connectTimerHandler, NULL);
    if (msgrtrWork.remoteConnectTimer == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radTimerCreate failed!");
        radProcessExit ();
        radSystemExit (msgrtrWork.radSystemID);
        exit (1);
    }

    // Do we need to initialize remote services?
    if (msgrtrWork.listenPort > 0)
    {
        // yes, open listen socket:
        msgrtrWork.server = radSocketServerCreate(msgrtrWork.listenPort);
        if (msgrtrWork.server == NULL)
        {
            radMsgLog(PRI_CATASTROPHIC, "radSocketServerCreate failed!");
            radProcessExit ();
            radSystemExit(msgrtrWork.radSystemID);
            exit (1);
        }

        // Add him to our wait list:
        radProcessIORegisterDescriptor(radSocketGetDescriptor(msgrtrWork.server),
                                       ServerRXHandler,
                                       NULL);
    }

    // Do we need to connect to a remote router?
    if (strlen(msgrtrWork.remoteIP) > 0)
    {
        // yes, just start the timer:
        radTimerStart(msgrtrWork.remoteConnectTimer, 50);
    }


    // enter normal processing
    radMsgLog(PRI_STATUS, "running...");


//**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**
    while (TRUE)
    {
        // Wait for activity on any of our descriptors:
        if (radProcessWait(0) == ERROR)
        {
            break;
        }
    }
//**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**~~**


    radMsgLog(PRI_STATUS, "exiting normally...");

    radProcessSetExitFlag ();
    msgrtrSysExit (&msgrtrWork);
    radProcessExit ();
    radSystemExit (msgrtrWork.radSystemID);
    exit (0);
}

