/*---------------------------------------------------------------------------
 
  FILENAME:
        radsortlist.c
 
  PURPOSE:
        Methods for the SortedList utility.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        11/14/98        MST             0               Original
        12/23/01        MST             1               Port to "C"
        02/08/2007      MST             2               Add 64-bit support
 
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
#include <radsysdefs.h>

/*  ... Local header files
*/
#include "radsortlist.h"


static long slDefaultKey (void *data)
{
    return ((long)data);
}


/*  ... define methods here
*/

SORTLIST_ID radSortListInit (long (*getKey)(void *data))
{
    SORTLIST_ID  newId;

    newId = (SORTLIST_ID) malloc (sizeof (*newId));
    if (newId == NULL)
    {
        return NULL;
    }
    memset (newId, 0, sizeof (*newId));

    radListReset (&newId->list);

    if (getKey == NULL)
    {
        newId->keyFunc = slDefaultKey;
    }
    else
    {
        newId->keyFunc = getKey;
    }

    return newId;
}

void radSortListExit (SORTLIST_ID id)
{
    NODE_PTR node;

    if (id == NULL)
    {
        return;
    }

    for (node = radListGetFirst (&id->list);
            node != NULL;
            node = radListGetFirst (&id->list))
    {
        radListRemove (&id->list, node);
        free (node);
    }

    free (id);
    return;
}

int radSortListInsert (SORTLIST_ID id, NODE_PTR newNode)
{
    NODE_PTR node;

    for (node = radListGetFirst (&id->list);
            node != NULL;
            node = radListGetNext (&id->list, node))
    {
        if ((*id->keyFunc)(node) > (*id->keyFunc)(newNode))
        {
            radListInsertBefore (&id->list, node, newNode);
            return OK;
        }
    }

    radListAddToEnd (&id->list, newNode);
    return OK;
}

int radSortListRemove (SORTLIST_ID id, NODE_PTR fnode)
{
    NODE_PTR node;

    for (node = radListGetFirst (&id->list);
            node != NULL;
            node = radListGetNext (&id->list, node))
    {
        if (node == fnode)
        {
            radListRemove (&id->list, node);
            return OK;
        }
    }

    return ERROR;
}

NODE_PTR radSortListFind (SORTLIST_ID id, long key)
{
    NODE_PTR node;

    for (node = radListGetFirst (&id->list);
            node != NULL;
            node = radListGetNext (&id->list, node))
    {
        if ((*id->keyFunc)(node) == key)
        {
            return node;
        }
    }

    return NULL;
}

