/*---------------------------------------------------------------------------
 
  FILENAME:
        radsocket.c
 
  PURPOSE:
        Provide the UDP socket utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        10/08/2005      M.S. Teel       0               Original
 
  NOTES:
        This utility opens and configures UDP sockets and passes data. Byte 
        order or content of the data is not considered. The user is responsible 
        for data contents and byte ordering conversions (if required).
 
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
/* #include <sys/select.h> */
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

/*  ... Library include files
*/
#include <radUDPsocket.h>

/*  ... Local include files
*/

/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/


/* ... methods
*/
/* ... Create a UDP socket (not bound for receive);
 ... returns RADUDPSOCK_ID or NULL if ERROR
*/
RADUDPSOCK_ID radUDPSocketCreate (void)
{
    RADUDPSOCK_ID       newId;
    int                 temp;
    struct sockaddr_in  sadrs;

    // first get our new object
    newId = (RADUDPSOCK_ID) malloc (sizeof (*newId));
    if (newId == NULL)
    {
        return NULL;
    }
    memset (newId, 0, sizeof (*newId));

    // create our socket
    newId->sockfd = socket (PF_INET, SOCK_DGRAM, 0);
    if (newId->sockfd == -1)
    {
        free (newId);
        return NULL;
    }

    // allow the port to be re-used in case we die and the socket lingers...
    temp = 1;
    if (setsockopt (newId->sockfd, SOL_SOCKET, SO_REUSEADDR, &temp, sizeof (temp))
            == -1)
    {
        close (newId->sockfd);
        free (newId);
        return NULL;
    }

    // set some default conditions
    radUDPSocketSetBroadcast (newId, FALSE);
    radUDPSocketSetUnicastTTL (newId, 1);
    radUDPSocketSetMulticastTTL (newId, 1);
    radUDPSocketSetMulticastLoopback (newId, FALSE);

    return newId;
}



/* ... Close connection and cleanup resources;
 ... returns OK or ERROR
*/
int radUDPSocketDestroy (RADUDPSOCK_ID id)
{
    shutdown (id->sockfd, 2);
    close (id->sockfd);
    free (id);
    return OK;
}


/*  ... Get the socket descriptor (for select calls, etc.)
*/
int radUDPSocketGetDescriptor (RADUDPSOCK_ID id)
{
    return id->sockfd;
}


/*  ... Bind the UDP socket to a local port so you can add the socket 
    ... descriptor to your select list and receive data
*/
int radUDPSocketBind (RADUDPSOCK_ID id, USHORT port)
{
    struct sockaddr_in  sa;

    memset (&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl (INADDR_ANY);
    sa.sin_port = htons (port);

    if (bind (id->sockfd, (struct sockaddr *)&sa, sizeof (sa)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketBind: bind failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... Enable/Disable sending broadcasts
*/
int radUDPSocketSetBroadcast (RADUDPSOCK_ID id, int enable)
{
    int         temp = (enable ? 1 : 0);

    if (setsockopt (id->sockfd, SOL_SOCKET, SO_BROADCAST, (char *)&temp, sizeof(temp)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSetBroadcast: failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... Set multicast interface for outgoing datagrams
    ... returns OK or ERROR
*/
int radUDPSocketSetMulticastTXInterface (RADUDPSOCK_ID id, char *interfaceIP)
{
    struct hostent  *hostentptr = NULL;
    struct in_addr  ifAdrs;

    hostentptr = gethostbyname (interfaceIP);
    if (hostentptr == NULL)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSetMulticastTXInterface: gethostbyname failed for %s", 
                   interfaceIP);
        return ERROR;
    }
    memcpy (&ifAdrs, hostentptr->h_addr, 4);

    if (setsockopt (id->sockfd, SOL_IP, IP_MULTICAST_IF, &ifAdrs, sizeof(ifAdrs)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSetMulticastTXInterface: failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... Set unicast TTL for outgoing datagrams (1 by default)
*/
int radUDPSocketSetUnicastTTL (RADUDPSOCK_ID id, int ttl)
{
    int          temp = ttl;

    if (setsockopt (id->sockfd, SOL_IP, IP_TTL, &temp, sizeof(int)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSetIPTTL: failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... Set multicast TTL for outgoing datagrams (1 by default)
*/
int radUDPSocketSetMulticastTTL (RADUDPSOCK_ID id, int ttl)
{
    UCHAR         temp = (UCHAR)ttl;

    if (setsockopt (id->sockfd, SOL_IP, IP_MULTICAST_TTL, (char *)&temp, sizeof(UCHAR)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSetMulticastTTL: failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... Enable/Disable multicast loopback (disabled by default)
*/
int radUDPSocketSetMulticastLoopback (RADUDPSOCK_ID id, int enable)
{
    UCHAR         temp = (enable ? 1 : 0);

    if (setsockopt(id->sockfd, SOL_IP, IP_MULTICAST_LOOP, (char *)&temp, sizeof(UCHAR)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSetMulticastLoopback: failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... "Turn on" RX multicast datagrams on the "multicastGroupIP" group and
    ... "interfaceIP" interface
*/
int radUDPSocketAddMulticastMembership
(
    RADUDPSOCK_ID   id,
    char            *multicastGroupIP,
    char            *interfaceIP
)
{
    struct ip_mreq  mcreq;
    struct hostent  *hostentptr = NULL;

    memset (&mcreq, 0, sizeof (mcreq));

    hostentptr = gethostbyname (multicastGroupIP);
    if (hostentptr == NULL)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketAddMulticastMembership: gethostbyname failed for %s", 
                   multicastGroupIP);
        return ERROR;
    }
    memcpy (&mcreq.imr_multiaddr.s_addr, hostentptr->h_addr, 4);

    hostentptr = gethostbyname (interfaceIP);
    if (hostentptr == NULL)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketAddMulticastMembership: gethostbyname failed for %s", 
                   interfaceIP);
        return ERROR;
    }
    memcpy (&mcreq.imr_interface.s_addr, hostentptr->h_addr, 4);

    if (setsockopt (id->sockfd, SOL_IP, IP_ADD_MEMBERSHIP, (char *)&mcreq, sizeof(struct ip_mreq)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketAddMulticastMembership: failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... "Turn off" RX multicast datagrams on the "multicastGroupIP" group and
    ... "interfaceIP" interface
*/
int radUDPSocketDropMulticastMembership
(
    RADUDPSOCK_ID   id,
    char            *multicastGroupIP,
    char            *interfaceIP
)
{
    struct ip_mreq  mcreq;
    struct hostent  *hostentptr = NULL;

    memset (&mcreq, 0, sizeof (mcreq));

    hostentptr = gethostbyname (multicastGroupIP);
    if (hostentptr == NULL)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketDropMulticastMembership: gethostbyname failed for %s", 
                   multicastGroupIP);
        return ERROR;
    }
    memcpy (&mcreq.imr_multiaddr.s_addr, hostentptr->h_addr, 4);

    hostentptr = gethostbyname (interfaceIP);
    if (hostentptr == NULL)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketDropMulticastMembership: gethostbyname failed for %s", 
                   interfaceIP);
        return ERROR;
    }
    memcpy (&mcreq.imr_interface.s_addr, hostentptr->h_addr, 4);

    if (setsockopt (id->sockfd, SOL_IP, IP_DROP_MEMBERSHIP, (char *)&mcreq, sizeof(struct ip_mreq)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketDropMulticastMembership: failed: %s", strerror(errno));
        return ERROR;
    }

    return OK;
}


/*  ... receive a datagram from a UDP socket;
    ... will return less than requested amount based on the size of the datagram;
    ... returns bytes read or ERROR if an error occurs
*/
int radUDPSocketRecvFrom
(
    RADUDPSOCK_ID   id,
    void            *buffer,
    int             maxToRead
)
{
    int             retVal;
    
    retVal = recvfrom (id->sockfd, buffer, maxToRead, MSG_DONTWAIT, NULL, 0);
    if (retVal != -1 && id->debug)
    {
        radMsgLog(PRI_STATUS, "<<<<<<<<<<<<<<<<<< radUDPSocketRecvFrom <<<<<<<<<<<<<<<<<<<<");
        radMsgLogData (buffer, retVal);
        radMsgLog(PRI_STATUS, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
    }

    return retVal;
}

/*  ... receive a datagram from a UDP socket;
    ... will return less than requested amount based on the size of the datagram;
    ... returns bytes read or ERROR if an error occurs
*/
int radUDPSocketReceiveFrom
(
    RADUDPSOCK_ID       id,
    void                *buffer,
    int                 maxToRead,
    struct sockaddr_in  *sourceAdrs
)
{
    int             retVal;
    socklen_t       adrsLength = sizeof(*sourceAdrs);
    
    retVal = recvfrom (id->sockfd, buffer, maxToRead, 
                       MSG_DONTWAIT, 
                       (struct sockaddr *)sourceAdrs, &adrsLength);
    if (retVal != -1 && id->debug)
    {
        radMsgLog(PRI_STATUS, "<<<<<<<<<<<<<<<< radUDPSocketReceiveFrom <<<<<<<<<<<<<<<<<<<");
        radMsgLogData (buffer, retVal);
        radMsgLog(PRI_STATUS, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
    }

    return retVal;
}


/*  ... Send a datagram to a host name or IP adrs and port (connectionless)
*/
int radUDPSocketSendTo
(
    RADUDPSOCK_ID       id,
    char                *hostOrIPAdrs,
    USHORT              port,
    void                *data,
    int                 length
)
{
    struct sockaddr_in  sa;
    struct hostent      *hostentptr = NULL;

    hostentptr = gethostbyname (hostOrIPAdrs);
    if (hostentptr == NULL)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSendTo: gethostbyname failed: %s", strerror(errno));
        return ERROR;
    }

    memset (&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons (port);
    sa.sin_addr = *((struct in_addr *)hostentptr->h_addr);

    if (sendto (id->sockfd, data, length, 0, (struct sockaddr *)&sa, sizeof (sa)) == -1)
    {
        radMsgLog(PRI_HIGH, "radUDPSocketSendTo: sendto failed: %s", strerror(errno));
        return ERROR;
    }

    if (id->debug)
    {
        radMsgLog(PRI_STATUS, ">>>>>>>>>>>>>>>>>>> radUDPSocketSendTo >>>>>>>>>>>>>>>>>>>>>");
        radMsgLogData (data, length);
        radMsgLog(PRI_STATUS, ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
    }

    return OK;
}


/*  ... Set the socket for blocking or non-blocking IO -
    ... it is the user's responsibility to handle blocking/non-blocking IO
    ... properly (EAGAIN and EINTR errno's);
    ... Returns OK or ERROR
*/
int radUDPSocketSetBlocking (RADUDPSOCK_ID id, int isBlocking)
{
    int             flags;

    if ((flags = fcntl (id->sockfd, F_GETFL, 0)) < 0)
        return ERROR;
    
    if (!isBlocking)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    
    if (fcntl (id->sockfd, F_SETFL, flags) < 0)
        return ERROR;
    
    return OK;
}

void radUDPSocketSetDebug (RADUDPSOCK_ID id, int enable)
{
    if (enable)
        id->debug = TRUE;
    else
        id->debug = FALSE;
}

