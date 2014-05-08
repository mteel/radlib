/*---------------------------------------------------------------------------
 
  FILENAME:
        radmsgLog.c
 
  PURPOSE:
        Provide the system message log utility.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        9/28/99         M.S. Teel       0               Original
        3/23/01         M.S. Teel       1               Port to Linux
 
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

/*      ... OS include files
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/*      ... System include files
*/

/*      ... Library include files
*/
#include <radtimeUtils.h>

/*      ... Local include files
*/
#include <radmsgLog.h>

/*      ... global memory declarations
*/

/*      ... global memory referenced
*/

/*      ... static (local) memory declarations
*/
static int      msTimestamp = 0;


/*  ... for use in system wide initialization ONLY
*/
int radMsgLogInit
(
    char        *procName,
    int         useStderr,      /* T/F: also write to stderr */
    int         timeStamp       /* T/F: include millisecond timestamps */
)
{
    int         options;

    msTimestamp = timeStamp;

    options = LOG_NDELAY | LOG_PID;
    if (useStderr)
    {
        options |= LOG_PERROR;
    }

    openlog (procName, options, LOG_USER);

    return OK;
}


int radMsgLogExit
(
    void
)
{
    closelog ();

    return OK;
}

/*  ... log a message - allow variable length parameter list
*/
int radMsgLog
(
    int         priority,
    char        *format,
    ...
)
{
    va_list     argList;
    char        temp1[512];
    int         index;

    if (msTimestamp)
    {
        index = sprintf (temp1, "<%llu> : ", radTimeGetMSSinceEpoch ());
    }
    else
    {
        index = 0;
    }

    /*  ... print the var arg stuff to the message
    */
    va_start (argList, format);
    vsprintf (&temp1[index], format, argList);
    va_end   (argList);

    syslog (priority, temp1);

    return OK;
}

void radMsgLogData (void *data, int length)
{
    char        msg[256], temp[16], temp1[16], ascii[128];
    int         i, j;
    UCHAR       *ptr = (UCHAR *)data;
    int         dataPresent = 1;

    radMsgLog(PRI_STATUS, "DBG: Dumping %p, %d bytes:", data, length);
    memset (msg, 0, sizeof (msg));
    memset (ascii, 0, sizeof(ascii));

    for (i = 0; i < length; i ++)
    {
        dataPresent = 1;
        sprintf (temp, "%2.2X", ptr[i]);
        sprintf (temp1, "%c", ((isprint(ptr[i]) ? ptr[i] : '.')));

        if (i % 2)
        {
            strcat (temp, " ");
        }

        if (i && ((i % 16) == 0))
        {
            // we need to dump a line
            strcat (msg, "    ");
            strcat (msg, ascii);
            radMsgLog(PRI_STATUS, msg);
            memset (msg, 0, sizeof(msg));
            memset (ascii, 0, sizeof(ascii));
            dataPresent = 0;
        }

        strcat (msg, temp);
        strcat (ascii, temp1);
    }

    if (dataPresent)
    {
        // we need to dump the last line
        for (j = (i%16); j != 0 && j < 16; j ++)
        {
            strcat (msg, "  ");
            if (j % 2)
                strcat (msg, " ");
        }
        strcat (msg, "    ");
        strcat (msg, ascii);
        radMsgLog(PRI_STATUS, msg);
    }

    return;
}

