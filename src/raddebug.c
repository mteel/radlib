/*---------------------------------------------------------------------------
 
  FILENAME:
        raddebug.c
 
  PURPOSE:
        Provide the system debugging utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        4/10/01         M.S. Teel       0               Original
 
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
#include <syslog.h>

/*      ... System include files
*/

/*      ... Library include files
*/

/*      ... Local include files
*/
#include <raddebug.h>

/*      ... global memory declarations
*/

/*      ... global memory referenced
*/

/*      ... static (local) memory declarations
*/


void radDEBUGPrint (int waitForInput, char *format, ...)
{
    va_list     argList;
    char        temp1[256];

    /*  ... print the var arg stuff to the message
    */
    va_start (argList, format);
    vsprintf (temp1, format, argList);
    va_end   (argList);

    if (waitForInput != 0)
    {
        printf ("<ENTER>: %s\n", temp1);
        getchar ();
    }
    else
    {
        printf ("%s\n", temp1);
    }

    return;
}

