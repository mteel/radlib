/*---------------------------------------------------------------------------
 
  FILENAME:
        radconffile.c
 
  PURPOSE:
        This file contains functions used to read/write config files.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        02/03/2004      MS Teel         0               Original
 
  NOTES:
        See conffile.h.
 
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

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <radsysdefs.h>
#include <radlist.h>
#include <radmsgLog.h>
#include <radbuffers.h>
#include <radconffile.h>


static char *textHdr[] =
    {
        "#\n",
        "#  This file contains configuration information.  The format of the\n",
        "#  file is not order dependent.\n",
        "#\n",
        "#  Comments are started by the # sign and continue to the end of the line.\n",
        "#  Blank lines are ignored.  The maximum line length is 256 characters.\n",
        "#  The general format for entries is as follows:\n",
        "#\n",
        "#  <IDENTIFIER>[<INSTANCE>](=)<VALUE>(whitespace)(# COMMENT)\n",
        "#\n",
        "#  IDENTIFIER: a text string identifying the data to follow.  Max size\n",
        "#              is 32 characters.  No whitespace characters allowed.\n",
        "#  INSTANCE:   (OPTIONAL) a text string creating a unique instance of \n",
        "#              IDENTIFIER. Entries may or may not have an instance.\n",
        "#              No whitespace characters allowed.\n",
        "#              If the instance is present, the content of the instance\n",
        "#              is application specific.  Max size is 32 characters.\n",
        "#  (=):        (OPTIONAL) The delimiter between the identifier and the value\n",
        "#              can be any amount of whitespace or an '=' sign.\n",
        "#  VALUE:      a text string containing application specific information.\n",
        "#              No whitespace characters allowed.\n",
        "#  COMMENT:    (OPTIONAL) separated from the value string by any amount\n",
        "#              of whitespace and started with the '#' character.\n",
        "#\n",
        "#\n",
        "\0"
    };

static void radCfInsertTextHdr(CF_ID file)
{
    CF_COMMENT_DATA_TYPE  *commentData;
    NODE_PTR              lastNode;
    int                   i;

    /*  Insert the text header at the front of the file data  */
    /*  Get a new buffer...  */
    commentData = radBufferGet(sizeof(CF_COMMENT_DATA_TYPE));
    if (commentData == NULL)
    {
        radMsgLog(PRI_HIGH, "radCfInsertTextHdr: could not allocate memory");
        return;
    }

    /*  Format it!  */
    commentData->hdr.type = CF_COMMENT;
    sprintf (commentData->comment, "#  Filename: %s\n", file->fileName);

    /*  Add it to the file data...  */
    radListAddToFront(file->fileData, &(commentData->hdr.node));

    for (i = 0, lastNode = &(commentData->hdr.node); *textHdr[i] != '\0'; i++)
    {
        /*  Get a new buffer...  */
        commentData = radBufferGet(sizeof(CF_COMMENT_DATA_TYPE));
        if (commentData == NULL)
        {
            radMsgLog(PRI_HIGH, "radCfInsertTextHdr: could not allocate comment memory %d", i);
            return;
        }

        /*  Format it!  */
        commentData->hdr.type = CF_COMMENT;
        strncpy(commentData->comment, textHdr[i], sizeof(commentData->comment));

        /*  Add it to the file data...  */
        radListInsertAfter(file->fileData, lastNode, &(commentData->hdr.node));
        lastNode = &(commentData->hdr.node);
    }

    return;
}


static CF_FILE_DATA_HDR *radCfFindEntryNodeAfter
(
    CF_ID               file,
    char                *id,
    char                *instance,
    CF_FILE_DATA_HDR    *thisOne
)
{
    CF_FILE_DATA_HDR    *hdr;
    CF_ENTRY_DATA_TYPE  *entry;

    /*  Start from the beginning and search for it...  */
    for (hdr = (CF_FILE_DATA_HDR *)radListGetNext(file->fileData, &(thisOne->node));
            hdr != NULL;
            hdr = (CF_FILE_DATA_HDR *)radListGetNext(file->fileData, &hdr->node))
    {
        /*  See if this node is for a data entry  */
        if (hdr->type != CF_ENTRY)
        {
            /*  Not a data entry...skip it!  */
            continue;
        }

        /*  Check the ID for a match  */
        entry = (CF_ENTRY_DATA_TYPE *)hdr;
        if (strcmp(entry->id, id) != 0)
        {
            /*  This wasn't the right one, move along!  */
            continue;
        }

        /*  if given, check to see if the instance matches  */
        if (instance != NULL)
        {
            if (strcmp(entry->instance, instance) != 0)
            {
                continue;
            }
        }

        return(hdr);
    }

    return(NULL);
}


static CF_FILE_DATA_HDR *radCfFindEntryNode(CF_ID file, char *id, char *instance)
{
    return(radCfFindEntryNodeAfter(file, id, instance, NULL));
}


static int radCfLockFile(char *file)
{
    char                  host[64];
    char                  lockFile[MAX_LINE_LENGTH];
    char                  lockLink[MAX_LINE_LENGTH];
    int                   i;
    char                  *temp;

    /*  Create a lock file...  */
    gethostname (host, 64);
    i = getpid ();
    sprintf (lockFile, "%s:%d", host, i);
    
    temp = strrchr (file, '/');
    if (temp == NULL)
    {
        temp = file;
    }
    else
    {
        temp ++;
    }
    sprintf(lockLink, "lock:%s", temp);
    if (symlink(lockFile, lockLink) != 0)
    {
        return(ERROR);
    }

    return(OK);
}


static int radCfUnlockFile(char *file)
{
    char                  lockLink[MAX_LINE_LENGTH];
    char                  *temp;

    /*  Remove lock file...  */
    temp = strrchr(file, '/');
    if (temp == NULL)
    {
        temp = file;
    }
    else
    {
        temp ++;
    }
    sprintf(lockLink, "lock:%s", temp);
    unlink(lockLink);
    return(OK);
}



/*  Opens the given config file, creating local record keeping  */
/*  about the file.  If the file does not already exist, a new file is  */
/*  created.  Returns the CF_ID or NULL indicating an error.  */
/*  NOTE: This function reads the entire file into a RAM copy for  */
/*        use by the rest of the configFile utilities.  Any changes  */
/*        made to the config file via these utils will NOT take  */
/*        effect until the application calls radCfClose!  */
CF_ID radCfOpen(char *file)
{
    CF_ID                   curCF;
    char                    tempText[MAX_LINE_LENGTH];
    char                    *tempPtr;
    char                    *curToken;
    char                    *instPtrStart, *instPtrEnd;
    FILE                    *cfStream;
    CF_COMMENT_DATA_TYPE    *comment;
    CF_ENTRY_DATA_TYPE      *entry;
    CF_FILE_DATA_HDR        *hdr;
    int                     i;

    /*  First, create the work area for this file  */
    curCF = (CF_ID)radBufferGet(sizeof(CF_INFO_TYPE));
    if (curCF == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radCfOpen: could not allocate memory for config file");
        return(NULL);
    }

    /*  Make sure it's all initialized to 0  */
    memset(curCF, 0, sizeof(CF_INFO_TYPE));

    /*  Open the file for reading, creating it if it does not exist  */
    cfStream = fopen(file, "a+");
    if (cfStream == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radCfOpen: could not open file \"%s\" (%d)",
                  file, errno);
        radBufferRls(curCF);
        return(NULL);
    }

    /*  Set the file stream so it's at the beginning of the file  */
    if (fseek(cfStream, 0, SEEK_SET) != 0)
    {
        radMsgLog(PRI_CATASTROPHIC, "radCfOpen: could not set file position to beginning");
        fclose(cfStream);
        radBufferRls(curCF);
        return(NULL);
    }

    /*  Save off the filename so we can close it out later...  */
    strncpy(curCF->fileName, file, MAX_FILENAME_LENGTH);

    /*  Create the list that will contain the file contents by line  */
    curCF->fileData = radListCreate();
    if (curCF->fileData == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radCfOpen: could not create list for file contents");
        fclose(cfStream);
        radBufferRls(curCF);
        return(NULL);
    }

    /*  Read the file into memory  */
    for (;;)
    {
        if (fgets(tempText, MAX_LINE_LENGTH, cfStream) == NULL)
        {
            /*  End of file reached, exit the loop!  */
            break;
        }

        /*  Move past any initial whitespace  */
        for (tempPtr = tempText; isspace(*tempPtr) && (*tempPtr != '\0'); tempPtr++)
            ;

        /*  See if this line is a comment or an entry...  */
        if (*tempPtr == '#')
        {
            /*  It's a comment...allocate some space for it  */
            comment = (CF_COMMENT_DATA_TYPE *)radBufferGet(sizeof(CF_COMMENT_DATA_TYPE));
            if (comment == NULL)
            {
                radMsgLog(PRI_CATASTROPHIC, "radCfOpen: could not allocate memory");

                /*  Need to cleanup everything!  */
                fclose(cfStream);
                radCfClose(curCF);
                return(NULL);
            }

            /*  Set the type to comment...  */
            comment->hdr.type = CF_COMMENT;

            /*  The rest is a simply copy!  */
            strncpy(comment->comment, tempPtr, sizeof(comment->comment));

            /*  Setup the header pointer for addition to the list later!  */
            hdr = (CF_FILE_DATA_HDR *)comment;
        }
        else if (*tempPtr == '\0')
        {
            /*  Treat a line of whitespace like a comment...  */
            comment = (CF_COMMENT_DATA_TYPE *)radBufferGet(sizeof(CF_COMMENT_DATA_TYPE));
            if (comment == NULL)
            {
                radMsgLog(PRI_CATASTROPHIC, "radCfOpen: could not allocate memory");

                /*  Need to cleanup everything!  */
                fclose(cfStream);
                radCfClose(curCF);
                return(NULL);
            }

            /*  Set the type to comment...  */
            comment->hdr.type = CF_COMMENT;

            /*  Copy the whitespace from the original buffer  */
            strncpy(comment->comment, tempText, sizeof(comment->comment));

            /*  Setup the header pointer for addition to the list later!  */
            hdr = (CF_FILE_DATA_HDR *)comment;
        }
        else
        {
            /*  Allocate memory for the entry...  */
            entry = (CF_ENTRY_DATA_TYPE *)radBufferGet(sizeof(CF_ENTRY_DATA_TYPE));
            if (entry == NULL)
            {
                radMsgLog(PRI_CATASTROPHIC, "radCfOpen: could not allocate memory");

                /*  Need to cleanup everything!  */
                fclose(cfStream);
                radCfClose(curCF);
                return(NULL);
            }

            memset (entry, 0, sizeof (*entry));

            /*  Set the type to data...  */
            entry->hdr.type = CF_ENTRY;

            /*  Pull out the id and optionally, the instance  */
            curToken = strtok(tempPtr, "= \t");
            if (curToken == NULL)
            {
                radMsgLog(PRI_MEDIUM, "radCfOpen: entry contains no IDENTIFIER...skipping");
                radBufferRls(entry);
                continue;
            }

            // is there an instance in this monkey?
            instPtrStart = strchr (curToken, '[');
            if (instPtrStart != NULL)
            {
                // yes, there is!
                instPtrStart ++;

                instPtrEnd = strchr (curToken, ']');
                if (instPtrEnd == NULL ||
                        (instPtrEnd - instPtrStart) > MAX_INSTANCE_LENGTH)
                {
                    radMsgLog(PRI_MEDIUM, "radCfOpen: entry contains invalid IDENTIFIER...skipping");
                    radBufferRls(entry);
                    continue;
                }

                if ((instPtrStart-1) - curToken > MAX_ID_LENGTH)
                {
                    radMsgLog (PRI_MEDIUM, "radCfOpen: entry contains invalid IDENTIFIER...skipping");
                    radBufferRls (entry);
                    continue;
                }

                strncpy (entry->id, curToken, (instPtrStart-1) - curToken);
                strncpy (entry->instance, instPtrStart, instPtrEnd - instPtrStart);
            }
            else
            {
                if (strlen (curToken) > MAX_ID_LENGTH)
                {
                    radMsgLog (PRI_MEDIUM, "radCfOpen: entry contains invalid IDENTIFIER...skipping");
                    radBufferRls (entry);
                    continue;
                }

                strncpy (entry->id, curToken, MAX_ID_LENGTH);
                entry->instance[0] = '\0';
            }


            /*  Get the value  */
            curToken = strtok (NULL, " #\t\n");
            if (curToken == NULL)
            {
                entry->value[0] = '\0';
            }
            else
            {
                strncpy (entry->value, curToken, MAX_LINE_LENGTH);
            }

            /*  is there a comment to pick up...  */
            curToken = strtok (NULL, "\n");
            if (curToken == NULL)
            {
                entry->comment[0] = '\0';
            }
            else
            {
                // skip whitespace
                while (*curToken && *curToken != '#')
                {
                    curToken ++;
                }

                // get rid of any newline, if found
                instPtrStart = strchr (curToken, '\n');
                if (instPtrStart != NULL)
                {
                    *instPtrStart = 0;
                }

                strncpy (entry->comment, curToken, MAX_COMMENT_LENGTH);
            }

            /*  Setup the header pointer for addition to the list later!  */
            hdr = (CF_FILE_DATA_HDR *)entry;
        }

        /*  Add the new line to the list  */
        radListAddToEnd(curCF->fileData, (NODE_PTR)&(hdr->node));
    }

    /*  Close the file...  */
    fclose(cfStream);

    /*  Done!  */
    return(curCF);
}


/*  Closes the file and cleans up local information about it.  */
void radCfClose(CF_ID file)
{
    CF_FILE_DATA_HDR      *hdr;

    /*  Now that we are ready, start de-allocating  */
    for (hdr = (CF_FILE_DATA_HDR *)radListGetFirst(file->fileData);
            hdr != NULL;
            hdr = (CF_FILE_DATA_HDR *)radListGetFirst(file->fileData))
    {
        /*  Remove the node from the list  */
        radListRemove(file->fileData, &(hdr->node));

        /*  Now that it is off the list, free the buffer  */
        radBufferRls(hdr);
    }

    radListDelete (file->fileData);
    radBufferRls (file);

    /*  DONE!  */
    return;
}


/*  Flushes the file to disk.  Returns OK or ERROR  */
/*  NOTE: This call MUST be made for any changes to config files to be  */
/*        saved into the file itself.  */
int radCfFlush(CF_ID file)
{
    CF_FILE_DATA_HDR      *hdr;
    CF_COMMENT_DATA_TYPE  *comment;
    CF_ENTRY_DATA_TYPE    *entry;
    FILE                  *radCfStream;

    /*  Lock the file...  */
    if (radCfLockFile(file->fileName) == ERROR)
    {
        return(ERROR);
    }

    /*  Open the file for writing  */
    radCfStream = fopen(file->fileName, "w");
    if (radCfStream == NULL)
    {
        /*  Unlock the file...  */
        radCfUnlockFile(file->fileName);

        radMsgLog(PRI_CATASTROPHIC, "radCfFlush: file \"%s\" could not be opened for writing",
                  file->fileName);

        return(ERROR);
    }

    /*  Make sure that a header is at the beginning of the file...  */
    comment = (CF_COMMENT_DATA_TYPE *)radListGetFirst(file->fileData);
    if ((comment == NULL) || (comment->hdr.type != CF_COMMENT))
    {
        /*  header information is missing!  Go add it!  */
        radCfInsertTextHdr(file);
    }

    /*  Now that we are ready, start writing and de-allocating  */
    for (hdr = (CF_FILE_DATA_HDR *)radListGetFirst(file->fileData);
            hdr != NULL;
            hdr = (CF_FILE_DATA_HDR *)radListGetNext(file->fileData, &(hdr->node)))
    {
        /*  The type tells me how to format the output in the file...  */
        switch (hdr->type)
        {
        case CF_COMMENT:
            {
                /*  The comment line can be dumped to the file just as it is...  */
                comment = (CF_COMMENT_DATA_TYPE *)hdr;
                if (fputs(comment->comment, radCfStream) == EOF)
                {
                    radMsgLog(PRI_CATASTROPHIC, "radCfFlush: error writing to file \"%s\"",
                              file->fileName);

                    /*  Close the file  */
                    fclose(radCfStream);

                    /*  Unlock the file...  */
                    radCfUnlockFile(file->fileName);

                    return(ERROR);
                }

                break;
            }

        case CF_ENTRY:
            {
                /*  The entry must be dumped to the file in a particular way...  */
                entry = (CF_ENTRY_DATA_TYPE *)hdr;
                if (strlen (entry->instance) > 0)
                {
                    fprintf(radCfStream, "%s[%s]=%s",
                            entry->id, entry->instance, entry->value);
                }
                else
                {
                    fprintf(radCfStream, "%s=%s",
                            entry->id, entry->value);
                }

                if (strlen (entry->comment) > 0)
                {
                    fprintf(radCfStream, "\t\t%s", entry->comment);
                }

                fprintf(radCfStream, "\n");
                break;
            }

        default:
            {
                radMsgLog(PRI_MEDIUM, "radCfFlush: unknown file data: %u", hdr->type);
                break;
            }
        }    /*  end of switch on type  */
    }

    /*  Close the file  */
    fclose(radCfStream);

    /*  Unlock the file...  */
    radCfUnlockFile(file->fileName);

    /*  DONE!  */
    return(OK);
}


/*  Creates a comment line in the config file containing the given text.  */
/*  Returns OK or ERROR.  */
int radCfPutComment(CF_ID file, char *text)
{
    CF_COMMENT_DATA_TYPE  *commentData;

    /*  Get a new buffer...  */
    commentData = radBufferGet(sizeof(CF_COMMENT_DATA_TYPE));
    if (commentData == NULL)
    {
        radMsgLog(PRI_HIGH, "radCfPutComment: could not allocate memory");
        return(ERROR);
    }

    /*  Format it!  */
    commentData->hdr.type = CF_COMMENT;
    strncpy(commentData->comment, text, MAX_LINE_LENGTH);

    /*  Add it to the file data...  */
    radListAddToEnd(file->fileData, &(commentData->hdr.node));

    return(OK);
}


/*  Inserts a comment before the given entry.  Returns OK or ERROR  */
int radCfPutCommentBefore(CF_ID file, char *id, char *instance, char *commentText)
{
    CF_FILE_DATA_HDR      *hdr;
    CF_COMMENT_DATA_TYPE  *commentData;

    /*  See if the node exists...  */
    if ((hdr = radCfFindEntryNode(file, id, instance)) == NULL)
    {
        return(ERROR);
    }

    /*  Found it!  Add the comment!  */
    /*  Get a new buffer...  */
    commentData = radBufferGet(sizeof(CF_COMMENT_DATA_TYPE));
    if (commentData == NULL)
    {
        radMsgLog(PRI_HIGH, "radCfPutCommentBefore: could not allocate memory");
        return(ERROR);
    }

    /*  Format it!  */
    commentData->hdr.type = CF_COMMENT;
    strncpy(commentData->comment, commentText, MAX_LINE_LENGTH);

    /*  Add the comment to the file data...  */
    radListInsertBefore(file->fileData, &(hdr->node), &(commentData->hdr.node));

    /*  Done, return OK  */
    return(OK);
}


/*  Checks for a comment before the given entry.  Returns TRUE or FALSE  */
int radCfIsCommentBefore(CF_ID file, char *id, char *instance, char *commentText)
{
    CF_FILE_DATA_HDR      *hdr;
    CF_COMMENT_DATA_TYPE  *commentData;

    /*  Find the entry's node  */
    if ((hdr = radCfFindEntryNode(file, id, instance)) == NULL)
    {
        return(FALSE);
    }

    /*  Now traverse the list backwards looking for the comment!  */
    for (hdr = (CF_FILE_DATA_HDR *)radListGetPrevious(file->fileData, &hdr->node);
            hdr != NULL;
            hdr = (CF_FILE_DATA_HDR *)radListGetPrevious(file->fileData, &hdr->node))
    {
        /*  See if this node is for a comment entry  */
        if (hdr->type != CF_COMMENT)
        {
            /*  Not a comment entry...skip it!  */
            continue;
        }

        /*  Check the text for a match  */
        commentData = (CF_COMMENT_DATA_TYPE *)hdr;
        if (strcmp(commentData->comment, commentText) != 0)
        {
            /*  This wasn't the right one, move along!  */
            continue;
        }

        return(TRUE);
    }

    return(FALSE);
}


/*  Checks for a comment after the given entry.  Returns TRUE or FALSE  */
int radCfIsCommentAfter(CF_ID file, char *id, char *instance, char *commentText)
{
    CF_FILE_DATA_HDR      *hdr;
    CF_COMMENT_DATA_TYPE  *commentData;

    /*  Find the entry's node  */
    if ((hdr = radCfFindEntryNode(file, id, instance)) == NULL)
    {
        return(FALSE);
    }

    /*  Now traverse the list forward looking for the comment!  */
    for (hdr = (CF_FILE_DATA_HDR *)radListGetNext(file->fileData, &hdr->node);
            hdr != NULL;
            hdr = (CF_FILE_DATA_HDR *)radListGetNext(file->fileData, &hdr->node))
    {
        /*  See if this node is for a comment entry  */
        if (hdr->type != CF_COMMENT)
        {
            /*  Not a comment entry...skip it!  */
            continue;
        }

        /*  Check the text for a match  */
        commentData = (CF_COMMENT_DATA_TYPE *)hdr;
        if (strcmp(commentData->comment, commentText) != 0)
        {
            /*  This wasn't the right one, move along!  */
            continue;
        }

        return(TRUE);
    }

    return(FALSE);
}


/*  Retrieves the first entry for a particular ID.  instance and value  */
/*  are both filled with the appropriate information when an entry is  */
/*  found.  Returns OK or ERROR.  */
int radCfGetFirstEntry(CF_ID file, char *id, char *instance, char *value)
{
    /*  Set the lastSearchNode to NULL  */
    file->lastSearchNode = NULL;

    /*  Just call radCfGetNextEntry!  */
    return(radCfGetNextEntry(file, id, instance, value));
}


/*  Gets the next entry for a particular ID.  instance and value are  */
/*  both filled with the appropriate information when a new entry is  */
/*  found.  Returns OK or ERROR.  */
int radCfGetNextEntry(CF_ID file, char *id, char *instance, char *value)
{
    CF_FILE_DATA_HDR    *hdr;
    CF_ENTRY_DATA_TYPE  *entry;

    /*  Use the lastSearchNode as a starting point...  */
    for (hdr = (CF_FILE_DATA_HDR *)radListGetNext(file->fileData, file->lastSearchNode);
            hdr != NULL;
            hdr = (CF_FILE_DATA_HDR *)radListGetNext(file->fileData, &hdr->node))
    {
        /*  See if this node is for a data entry  */
        if (hdr->type != CF_ENTRY)
        {
            /*  Not a data entry...skip it!  */
            continue;
        }

        /*  Check the ID for a match  */
        entry = (CF_ENTRY_DATA_TYPE *)hdr;
        if (strcmp(entry->id, id) != 0)
        {
            continue;
        }

        /*  Found it!  Copy over instance and value  */
        if (instance != NULL)
        {
            strncpy(instance, entry->instance, MAX_INSTANCE_LENGTH);
        }

        strncpy(value, entry->value, MAX_LINE_LENGTH);

        /*  Save off this node for future reference!  */
        file->lastSearchNode = &(entry->hdr.node);

        /*  Done, return OK  */
        return(OK);
    }

    return(ERROR);
}


/*  Retrieves a particular config file entry specified by the "id" and  */
/*  "instance" inputs.  Returns OK or ERROR.  */
int radCfGetEntry(CF_ID file, char *id, char *instance, char *value)
{
    CF_FILE_DATA_HDR    *hdr;
    CF_ENTRY_DATA_TYPE  *entry;

    /*  See if the node exists...  */
    if ((hdr = radCfFindEntryNode(file, id, instance)) == NULL)
    {
        return(ERROR);
    }

    /*  Found it!  Copy over value  */
    entry = (CF_ENTRY_DATA_TYPE *)hdr;
    strncpy(value, entry->value, MAX_LINE_LENGTH);

    /*  Done, return OK  */
    return(OK);
}


/*  Creates/Updates a configuration entry in the file  */
/*  given the id, instance, and value.  Returns OK or ERROR  */
int radCfPutEntry(CF_ID file, char *id, char *instance, char *value, char *comment)
{
    CF_FILE_DATA_HDR    *hdr;
    CF_ENTRY_DATA_TYPE  *entry;

    /*  See if the node exists...  */
    if ((hdr = radCfFindEntryNode(file, id, instance)) == NULL)
    {
        /*  Then entry doesn't exist!  Create a new one!  */
        /*  Get a new buffer...  */
        entry = radBufferGet(sizeof(CF_ENTRY_DATA_TYPE));
        if (entry == NULL)
        {
            radMsgLog(PRI_HIGH, "radCfPutEntry: could not allocate memory");
            return(ERROR);
        }

        /*  Format it!  */
        entry->hdr.type = CF_ENTRY;
        strncpy(entry->id, id, MAX_ID_LENGTH);
        if (instance != NULL)
        {
            strncpy(entry->instance, instance, MAX_INSTANCE_LENGTH);
        }
        else
        {
            entry->instance[0] = 0;
        }
        strncpy(entry->value, value, MAX_LINE_LENGTH);

        if (comment != NULL)
        {
            sprintf(entry->comment, "# %s", comment);
        }
        else
        {
            entry->comment[0] = '\0';
        }

        /*  Add it to the file data...  */
        radListAddToEnd(file->fileData, &(entry->hdr.node));
        return(OK);
    }

    /*  This one already exists...just update it  */
    entry = (CF_ENTRY_DATA_TYPE *)hdr;
    strncpy(entry->value, value, MAX_LINE_LENGTH);
    if (comment != NULL)
    {
        sprintf(entry->comment, "# %s", comment);
    }

    /*  Done, return OK  */
    return(OK);
}

