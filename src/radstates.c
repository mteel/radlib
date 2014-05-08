/*---------------------------------------------------------------------------
 
  FILENAME:
        radstates.c
 
  PURPOSE:
        Provide the state machine utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        11/15/99        M.S. Teel       0               Original
        3/22/01         M.S. Teel       1               Port to Linux
 
  NOTES:
        See states.h for details.
 
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

/*      ... System include files
*/
#include <radsysdefs.h>
#include <radmsgLog.h>

/*      ... Library include files
*/

/*      ... Local include files
*/
#include <radstates.h>

/*      ... global memory declarations
*/

/*      ... global memory referenced
*/

/*      ... static (local) memory declarations
*/
static int statesStub
(
    int     state,
    void    *stimulus,
    void    *userData
)
{
    radMsgLog(PRI_MEDIUM, "statesStub: no handler defined!");
    return state;
}



STATES_ID radStatesInit (void *saveData)
{
    STATES_ID       newData;
    int             i;

    newData = (STATES_ID) malloc (sizeof (STATES));
    if (newData == NULL)
    {
        radMsgLog(PRI_HIGH, "radStatesInit: malloc failed");
        return NULL;
    }

    memset (newData, 0, sizeof (STATES));
    newData->userData = saveData;

    for (i = 0; i < STATE_MAX_STATES; i ++)
    {
        newData->stateProc[i] = statesStub;
    }

    return newData;
}

void radStatesExit (STATES_ID id)
{
    if (id != NULL)
    {
        free (id);
    }
    return;
}


int radStatesAddHandler
(
    STATES_ID       id,
    int             state,
    int             (*handler)
    (
        int     state,
        void    *stimulus,
        void    *userData
    )
)
{
    if (state < 0 || state >= STATE_MAX_STATES)
    {
        radMsgLog(PRI_HIGH, "radStatesAddHandler: invalid state given");
        return ERROR;
    }

    if (handler == NULL)
    {
        radMsgLog(PRI_HIGH, "radStatesAddHandler: invalid handler given");
        return ERROR;
    }

    id->stateProc[state] = handler;
    return OK;
}

int radStatesRemHandler (STATES_ID id, int state)
{
    if (state < 0 || state >= STATE_MAX_STATES)
    {
        radMsgLog(PRI_HIGH, "radStatesRemHandler: invalid state given");
        return ERROR;
    }

    id->stateProc[state] = statesStub;
    return OK;
}

void radStatesProcess (STATES_ID id, void *stimulus)
{
    id->mstate = (*id->stateProc[id->mstate])
                 (id->mstate, stimulus, id->userData);
    return;
}

int radStatesSetState (STATES_ID id, int state)
{
    if (state < 0 || state >= STATE_MAX_STATES)
    {
        radMsgLog(PRI_MEDIUM, "radStatesSetState: invalid state given");
        return ERROR;
    }

    id->mstate = state;
    return OK;
}

int radStatesGetState (STATES_ID id)
{
    return id->mstate;
}

void radStatesReset (STATES_ID id, void *saveData)
{
    int     i;

    for (i = 0; i < STATE_MAX_STATES; i ++)
    {
        id->stateProc[i] = statesStub;
    }

    return;
}

