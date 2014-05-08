/*---------------------------------------------------------------------------
 
  FILENAME:
        radtextsearch.c
 
  PURPOSE:
        Provide methods for the red-black tree based text matching utility.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        1/7/2009        MST             0               Original
 
  NOTES:
        The inspiration for these algorithms was taken from Julienne Walker.
 
  LICENSE:
        Copyright 2001-2009 Mark S. Teel. All rights reserved.
 
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

// System header files:
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Local header files:
#include <radsysdefs.h>
#include <radmsgLog.h>
#include <radtextsearch.h>



static SEARCH_NODE* makeNode (const char* text, int ordinal)
{
    SEARCH_NODE*    newNode;

    newNode = malloc(sizeof(SEARCH_NODE));
    if (newNode == NULL)
    {
        return NULL;
    }

    memset(newNode, 0, sizeof(SEARCH_NODE));
    newNode->red        = 1;
    newNode->ordinal    = ordinal;
    strncpy(newNode->text, text, SEARCH_TEXT_MAX);
    return newNode;
}

static SEARCH_NODE* rotateSingle (SEARCH_NODE *root, int dir)
{
    SEARCH_NODE*    save = root->link[!dir];

    root->link[!dir] = save->link[dir];
    save->link[dir] = root;

    root->red = 1;
    save->red = 0;

    return save;
}

static SEARCH_NODE* rotateDouble (SEARCH_NODE *root, int dir)
{
    root->link[!dir] = rotateSingle(root->link[!dir], !dir);
    return rotateSingle(root, dir);
}

static int nodeIsRed (SEARCH_NODE* root)
{
    return (root != NULL && root->red == 1);
}


// Public methods:

TEXT_SEARCH_ID radtextsearchInit (void)
{
    TEXT_SEARCH_ID  newId;

    newId = malloc(sizeof(*newId));
    if (newId == NULL)
    {
        return NULL;
    }

    newId->root = NULL;
    return newId;
}


void radtextsearchExit (TEXT_SEARCH_ID id)
{
    // Empty the tree:
    while (radtextsearchRemove(id, NULL) == OK)
        ;

    // Delete ID:
    free(id);

    return;
}


int radtextsearchInsert (TEXT_SEARCH_ID id, const char* text, int ordinal)
{
    SEARCH_NODE     head = {0};         /* False tree root */
    SEARCH_NODE     *parent, *gparent;  /* Grandparent & parent */
    SEARCH_NODE     *t, *q;             /* Iterators */
    int             dir = 0, last;

    if (id->root == NULL)
    {
        // Empty tree:
        id->root = makeNode(text, ordinal);
        if (id->root == NULL)
        {
            return ERROR;
        }
    }
    else
    {
        // Set up pointers:
        t = &head;
        gparent = parent = NULL;
        q = t->link[1] = id->root;

        // Search down the tree:
        for ( ; ; )
        {
            if (q == NULL)
            {
                // Insert new node at the bottom:
                parent->link[dir] = q = makeNode(text, ordinal);
                if (q == NULL)
                {
                    return ERROR;
                }
            }
            else if (nodeIsRed(q->link[0]) && nodeIsRed(q->link[1]))
            {
                // Color flip:
                q->red = 1;
                q->link[0]->red = 0;
                q->link[1]->red = 0;
            }

            // Fix red violation:
            if (nodeIsRed(q) && nodeIsRed(parent))
            {
                int dir2 = t->link[1] == gparent;

                if (q == parent->link[last])
                    t->link[dir2] = rotateSingle(gparent, !last);
                else
                    t->link[dir2] = rotateDouble(gparent, !last);
            }

            // Stop if found:
            if (! strncmp(q->text, text, SEARCH_TEXT_MAX))
            {
                break;
            }

            last = dir;
            dir = ((strncmp(text, q->text, SEARCH_TEXT_MAX) > 0) ? 1 : 0);

            // Update pointers:
            if (gparent != NULL)
                t = gparent;
            gparent = parent, parent = q;
            q = q->link[dir];
        }

        // Update root:
        id->root = head.link[1];
    }

    // Make root black:
    id->root->red = 0;

    return OK;
}

int radtextsearchRemove (TEXT_SEARCH_ID id, const char* text)
{
    SEARCH_NODE     head = {0};                 /* False tree root */
    SEARCH_NODE     *q, *parent, *gparent;
    SEARCH_NODE     *found = NULL;
    int             retVal, last, dir = 1;

    if (id->root == NULL)
    {
        return ERROR;
    }

    // Set up pointers:
    q = &head;
    gparent = parent = NULL;
    q->link[1] = id->root;

    // Search and push a red down:
    while (q->link[dir] != NULL)
    {
        last = dir;

        gparent = parent, parent = q;
        q = q->link[dir];

        // Allow for NULL text specified:
        if (text == NULL)
        {
            dir = 0;

            // Just remove the first node we come to:
            found = q;
        }
        else
        {
            retVal = strncmp(text, q->text, SEARCH_TEXT_MAX);
            dir = ((retVal > 0) ? 1 : 0);

            // Save found node:
            if (retVal == 0)
            {
                found = q;
            }
        }

        // Push the red node down:
        if (!nodeIsRed(q) && !nodeIsRed(q->link[dir]))
        {
            if (nodeIsRed(q->link[!dir]))
            {
                parent = parent->link[last] = rotateSingle(q, dir);
            }
            else if (!nodeIsRed(q->link[!dir]))
            {
                SEARCH_NODE *s = parent->link[!last];

                if (s != NULL)
                {
                    if (!nodeIsRed(s->link[!last]) && !nodeIsRed(s->link[last]))
                    {
                        // Color flip:
                        parent->red = 0;
                        s->red = 1;
                        q->red = 1;
                    }
                    else
                    {
                        int dir2 = gparent->link[1] == parent;

                        if (nodeIsRed(s->link[last]))
                            gparent->link[dir2] = rotateDouble(parent, last);
                        else if (nodeIsRed(s->link[!last]))
                            gparent->link[dir2] = rotateSingle(parent, last);

                        // Ensure correct coloring:
                        q->red = gparent->link[dir2]->red = 1;
                        gparent->link[dir2]->link[0]->red = 0;
                        gparent->link[dir2]->link[1]->red = 0;
                    }
                }
            }
        }
    }

    // Replace and remove if found:
    if (found != NULL)
    {
        strncpy(found->text, q->text, SEARCH_TEXT_MAX);
        found->ordinal = q->ordinal;
        parent->link[parent->link[1] == q] = q->link[q->link[0] == NULL];
        free(q);
    }

    // Update root and make it black:
    id->root = head.link[1];
    if (id->root != NULL)
    {
        id->root->red = 0;
    }

    return OK;
}


int radtextsearchFind (TEXT_SEARCH_ID id, const char* text, int* ordinalStore)
{
    register SEARCH_NODE    *q;
    int                     retVal, dir;

    if (id->root == NULL)
    {
        return ERROR;
    }

    q = id->root;

    // Search:
    while (q != NULL)
    {
        retVal = strncmp(text, q->text, SEARCH_TEXT_MAX);
        dir = ((retVal > 0) ? 1 : 0);

        if (retVal == 0)
        {
            // Found it - return the ordinal:
            *ordinalStore = q->ordinal;
            return OK;
        }

        q = q->link[dir];
    }

    // If we are here, not found:
    return ERROR;
}

int radtextsearchDebug (SEARCH_NODE* root)
{
    int             lh, rh;
    int             numNodes = 0;
    SEARCH_NODE     *ln, *rn;

    if (root == NULL)
        return 1;
    else
    {
        ln = root->link[0];
        rn = root->link[1];

        /* Consecutive red links */
        if (nodeIsRed(root))
        {
            if (nodeIsRed(ln) || nodeIsRed(rn))
            {
                radMsgLog(PRI_MEDIUM, "radtextsearchDebug: Red violation!");
                return 0;
            }
        }

        lh = radtextsearchDebug(ln);
        rh = radtextsearchDebug(rn);

        /* Invalid binary search tree */
        if ((ln != NULL && strncmp(ln->text, root->text, SEARCH_TEXT_MAX) >= 0) || 
            (rn != NULL && strncmp(root->text, rn->text, SEARCH_TEXT_MAX) >= 0))
        {
            radMsgLog(PRI_MEDIUM, "radtextsearchDebug: Binary tree violation!");
            return 0;
        }

        /* Black height mismatch */
        if (lh != 0 && rh != 0 && lh != rh)
        {
            radMsgLog(PRI_MEDIUM, "radtextsearchDebug: Black violation!");
            return 0;
        }

        /* Only count black links */
        if (lh != 0 && rh != 0)
            return ((nodeIsRed(root)) ? lh : lh + 1);
        else
            return 0;
    }
}

