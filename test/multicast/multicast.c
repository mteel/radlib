#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <radsysdefs.h>
#include <radUDPsocket.h>

#define TEST_PORT                  14014

typedef struct
{
    char             myIP[16];
    char             randData[32];
} MCTEST_PKT;

int main (int argc, char *argv[])
{
    RADUDPSOCK_ID    sockId;
    MCTEST_PKT       pkt;
    int              sendcnt = 0;
    int              retVal;

    if (argc < 3)
    {
        printf ("\nYou must specify a local interface IP address and multicast group IP address...\n");
        printf ("Usage: multicast [local_intf_ip_address] [multicast_group_ip_address]\n");
        return 1;
    }

    sockId = radUDPSocketCreate ();
    if (sockId == NULL)
    {
        printf ("\nradUDPSocketCreate failed!\n");
        return 1;
    }

    if (radUDPSocketBind (sockId, TEST_PORT) == ERROR)
    {
        printf ("\nradUDPSocketBind failed!\n");
        radUDPSocketDestroy (sockId);
        return 1;
    }

    if (radUDPSocketSetMulticastTXInterface (sockId, argv[1]) == ERROR)
    {
        printf ("\radUDPSocketSetMulticastTXInterface failed!\n");
        radUDPSocketDestroy (sockId);
        return 1;
    }

    if (radUDPSocketSetMulticastTTL (sockId, 4) == ERROR)
    {
        printf ("\nradUDPSocketSetMulticastTTL failed!\n");
        radUDPSocketDestroy (sockId);
        return 1;
    }

    if (radUDPSocketAddMulticastMembership (sockId, argv[2], argv[1]) == ERROR)
    {
        printf ("\nradUDPSocketAddMulticastMembership failed!\n");
        radUDPSocketDestroy (sockId);
        return 1;
    }

    if (radUDPSocketSetBlocking (sockId, TRUE) == ERROR)
    {
        printf ("\nradUDPSocketSetBlocking failed!\n");
        radUDPSocketDestroy (sockId);
        return 1;
    }

    memset (&pkt, 0, sizeof (pkt));
    strncpy (pkt.myIP, argv[1], sizeof(pkt.myIP));
    strcpy (pkt.randData, "First packet...");

    if (radUDPSocketSendTo (sockId, argv[2], TEST_PORT, &pkt, sizeof (pkt)) == ERROR)
    {
        printf ("\nradUDPSocketSendTo failed!\n");
        radUDPSocketDestroy (sockId);
        return 1;
    }

    printf ("\n");
    for (;;)
    {
        retVal = radUDPSocketRecvFrom (sockId, &pkt, sizeof (pkt));
        if (retVal == sizeof (pkt))
        {
            printf ("RX: %s: %s\n", pkt.myIP, pkt.randData);
        }
        else if (retVal == -1)
        {
            sleep (2);
            continue;
        }
        else
        {
            printf ("RX UNKNOWN: %d bytes\n", retVal);
        }

        sleep (1);

        memset (&pkt, 0, sizeof (pkt));
        strncpy (pkt.myIP, argv[1], sizeof(pkt.myIP));
        sprintf (pkt.randData, "Packet %d", ++sendcnt);

        if (radUDPSocketSendTo (sockId, argv[2], TEST_PORT, &pkt, sizeof (pkt)) == ERROR)
        {
            printf ("radUDPSocketSendTo failed!\n");
            radUDPSocketDestroy (sockId);
            return 1;
        }
    }

    return 0;
}
