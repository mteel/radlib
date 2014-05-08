/*---------------------------------------------------------------------------
 
  FILENAME:
        radlist.c
 
  PURPOSE:
        Provide the linked list utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        5/26/95         M.S. Teel       0               Original
        9/26/99         MS Teel         1               Port to pSOS
        3/22/01         M.S. Teel       2               Port to Linux
 
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

/*      ... System include files
*/
#include <radsysdefs.h>
#include <radlist.h>

/*      ... Library include files
*/

/*      ... Local include files
*/


/*      ... global memory declarations
*/

/*      ... global memory referenced
*/

/*      ... static (local) memory declarations
*/



/*  ... Allocate and initialize a list to empty state
*/
RADLIST_ID radListCreate (void)
{
    RADLIST_ID  list;


    if ((list = (RADLIST_ID) malloc (sizeof (*list))) != NULL)
    {
        list->firstNode = &list->dummyFirst;
        list->lastNode = &list->dummyLast;
        list->firstNode->prevNode = NULL;
        list->firstNode->nextNode = list->lastNode;
        list->lastNode->prevNode = list->firstNode;
        list->lastNode->nextNode = NULL;
        list->noNodes = 0;
    }

    return (list);
}

RADLIST_ID radListReset
(
    RADLIST_ID  list
)
{
    list->firstNode = &list->dummyFirst;
    list->lastNode = &list->dummyLast;
    list->firstNode->prevNode = NULL;
    list->firstNode->nextNode = list->lastNode;
    list->lastNode->prevNode = list->firstNode;
    list->lastNode->nextNode = NULL;
    list->noNodes = 0;

    return (list);
}



void radListDelete
(
    RADLIST_ID  list
)
{
    free (list);
    return;
}


void radListInsertAfter
(
    RADLIST_ID  list,
    NODE_PTR    afterThisNode,
    NODE_PTR    node
)
{
    node->nextNode = afterThisNode->nextNode;
    node->prevNode = afterThisNode;
    afterThisNode->nextNode = node;
    node->nextNode->prevNode = node;
    list->noNodes += 1;
    return;
}


void radListInsertBefore
(
    RADLIST_ID  list,
    NODE_PTR    beforeThisNode,
    NODE_PTR    node
)
{

    node->prevNode = beforeThisNode->prevNode;
    node->nextNode = beforeThisNode;
    beforeThisNode->prevNode = node;
    node->prevNode->nextNode = node;
    list->noNodes += 1;
    return;
}


void radListAddToFront
(
    RADLIST_ID  list,
    NODE_PTR    node
)
{
    node->prevNode = list->firstNode;
    node->nextNode = list->firstNode->nextNode;
    list->firstNode->nextNode = node;
    node->nextNode->prevNode = node;
    list->noNodes += 1;
    return;
}


void radListAddToEnd
(
    RADLIST_ID  list,
    NODE_PTR    node
)
{
    node->prevNode = list->lastNode->prevNode;
    node->nextNode = list->lastNode;
    list->lastNode->prevNode = node;
    node->prevNode->nextNode = node;
    list->noNodes += 1;
    return;
}


void radListRemove
(
    RADLIST_ID  list,
    NODE_PTR    node
)
{
    node->nextNode->prevNode = node->prevNode;
    node->prevNode->nextNode = node->nextNode;
    list->noNodes -= 1;
    return;
}


NODE_PTR radListRemoveFirst
(
    RADLIST_ID  list
)
{
    NODE_PTR    node;


    if (list->firstNode->nextNode == list->lastNode)
        return (NULL);

    node = list->firstNode->nextNode;
    list->firstNode->nextNode = node->nextNode;
    node->nextNode->prevNode = list->firstNode;
    list->noNodes -= 1;
    return (node);
}


NODE_PTR radListRemoveLast
(
    RADLIST_ID  list
)
{
    NODE_PTR    node;


    if (list->lastNode->prevNode == list->firstNode)
        return (NULL);

    node = list->lastNode->prevNode;
    list->lastNode->prevNode = node->prevNode;
    node->prevNode->nextNode = list->lastNode;
    list->noNodes -= 1;
    return (node);
}


int radListGetNumberOfNodes
(
    RADLIST_ID  list
)
{
    return (list->noNodes);
}


NODE_PTR radListGetFirst
(
    RADLIST_ID  list
)
{
    if (list->firstNode->nextNode == list->lastNode)
        return (NULL);

    return (list->firstNode->nextNode);
}


NODE_PTR radListGetLast
(
    RADLIST_ID  list
)
{
    if (list->lastNode->prevNode == list->firstNode)
        return (NULL);
    return (list->lastNode->prevNode);
}


NODE_PTR radListGetNext
(
    RADLIST_ID  list,
    NODE_PTR    afterThisNode           /* NULL gets first one  */
)
{
    if (afterThisNode == NULL)
        return (radListGetFirst (list));

    if (afterThisNode->nextNode == list->lastNode)
        return (NULL);
    else
        return (afterThisNode->nextNode);
}


NODE_PTR radListGetPrevious
(
    RADLIST_ID  list,
    NODE_PTR    beforeThisNode          /* NULL gets first one  */
)
{
    if (beforeThisNode == NULL)
        return (radListGetLast (list));

    if (beforeThisNode->prevNode == list->firstNode)
        return (NULL);
    else
        return (beforeThisNode->prevNode);
}

#if 0
void listDebug
(
    RADLIST_ID list
)
{
    printf ("dfirst=%x,dlast=%x,first=%x,last=%x,dummyFirst=%x,dummyLast=%x\n",
            list->firstNode,
            list->lastNode,
            list->firstNode->nextNode,
            list->lastNode->prevNode,
            &list->dummyFirst,
            &list->dummyLast);
}
#endif
