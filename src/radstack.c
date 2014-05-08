/*---------------------------------------------------------------------------
 
  FILENAME:
        radstack.c
 
  PURPOSE:
        Provide methods for the stack utility.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        11/16/98        MST             0               Original
        12/9/01         MST             1               Port to C
 
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

/*  ... System header files
*/
#include <stdlib.h>
#include <stdio.h>

/*  ... Local header files
*/
#include <radsysdefs.h>
#include "radstack.h"


/*  ... define methods here
*/
STACK_ID radStackInit (void)
{
    STACK_ID newId;

    newId = (STACK_ID) malloc (sizeof (*newId));
    if (newId == NULL)
    {
        return NULL;
    }

    newId->head = NULL;
    newId->count = 0;
    return newId;
}


void radStackExit (STACK_ID id)
{
    STACK_NODE  *node;

    if (id == NULL)
    {
        return;
    }

    for (node = id->head; node != NULL; node = id->head)
    {
        id->head = node->next;
        free (node);
    }

    free (id);
    return;
}


/*  ... Push data onto stack
*/
int radStackPush (STACK_ID id, STACK_NODE *newNode)
{
    newNode->next = id->head;
    id->head = newNode;

    id->count ++;
    return OK;
}


/*  ... Pop data off the stack
*/
STACK_NODE *radStackPop (STACK_ID id)
{
    STACK_NODE  *node = NULL;

    if (id->head != NULL)
    {
        node = id->head;
        id->head = id->head->next;
        id->count --;
    }

    return node;
}

int radStackCount (STACK_ID id)
{
    return id->count;
}
