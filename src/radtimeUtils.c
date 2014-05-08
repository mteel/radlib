/*---------------------------------------------------------------------------
 
  FILENAME:
        radtimeUtils.c
 
  PURPOSE:
        This file contains utilities used to retrieve system time-of-day.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        04/23/03        MS Teel         0               Initial creation
        04/07/2008      M.S. Teel       1               Change return value of
                                                        radTimeGetMSSinceEpoch
                                                        to ULONGLONG
 
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

#include <radtimeUtils.h>


ULONGLONG radTimeGetMSSinceEpoch (void)
{
    struct timeval  tv;
    ULONGLONG       msec;

    /*  First, get the time  */
    gettimeofday (&tv, NULL);

    /*  Now calculate the number of milliseconds  */
    msec = (ULONGLONG)tv.tv_sec;
    msec *= 1000ULL;
    msec += (ULONGLONG)(tv.tv_usec / 1000);
    return msec;
}


ULONG radTimeGetSECSinceEpoch (void)
{
    struct timeval  tv;
    ULONG           sec;

    /*  First, get the time  */
    gettimeofday (&tv, NULL);

    sec = tv.tv_sec;
    return sec;
}

