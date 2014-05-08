/*-----------------------------------------------------------------------
 
  FILENAME:
        raddatabase.c
 
  PURPOSE:
        Provide the database I/F utilities - GENERIC.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Function
        01/02/02        M.S. Teel       0               Original
 
  ASSUMPTIONS:
        See raddatabase.h for details...
 
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

------------------------------------------------------------------------*/

/*  ... System include files
*/

/*  ... Library include files
*/
#include <radmsgLog.h>

/*  ... Local include files
*/
#include <raddatabase.h>


/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/

/* ... methods
*/

/**************************** Row - Level *********************************
 **************************************************************************
*/

/*  ... traverse the result set row by row
*/
ROW_ID raddatabaseResultsGetFirst (RESULT_SET_ID id)
{
    id->current = (ROW_ID)radListGetFirst (&id->rows);
    return id->current;
}

ROW_ID raddatabaseResultsGetNext (RESULT_SET_ID id)
{
    id->current = (ROW_ID)radListGetNext (&id->rows, (NODE_PTR)id->current);
    return id->current;
}

ROW_ID raddatabaseResultsGetPrev (RESULT_SET_ID id)
{
    id->current = (ROW_ID)radListGetPrevious (&id->rows, (NODE_PTR)id->current);
    return id->current;
}

ROW_ID raddatabaseResultsGetLast (RESULT_SET_ID id)
{
    id->current = (ROW_ID)radListGetLast (&id->rows);
    return id->current;
}

/*  ... create a row description to use when creating a new table;
    ... returns ROW_ID or NULL
*/
ROW_ID raddatabaseRowDescriptionCreate
(
    void
)
{
    ROW_ID      newId;

    newId = (ROW_ID) malloc (sizeof (*newId));
    if (newId == NULL)
    {
        radMsgLog(PRI_MEDIUM, "raddatabaseRowDescriptionCreate: malloc failed!");
        return NULL;
    }

    memset (newId, 0, sizeof (*newId));

    radListReset (&newId->fields);

    return newId;
}


/*  ... returns OK or ERROR
*/
int raddatabaseRowDescriptionAddField
(
    ROW_ID          id,
    const char      *name,
    UINT            type,
    int             maxLength
)
{
    FIELD_ID        field;

    field = (FIELD_ID) malloc (sizeof (*field));
    if (field == NULL)
    {
        radMsgLog(PRI_MEDIUM, "raddatabaseRowDescriptionCreate: malloc failed!");
        return ERROR;
    }
    memset (field, 0, sizeof (*field));

    strncpy (field->name, name, DB_FIELD_NAME_MAX-1);
    field->type = type;
    field->cvalLength = maxLength;

    radListAddToEnd (&id->fields, (NODE_PTR)field);
    return OK;
}

int raddatabaseRowDescriptionRemoveField
(
    ROW_ID          id,
    const char      *name
)
{
    FIELD_ID        field;

    field = raddatabaseFieldGet (id, name);
    if (field == NULL)
    {
        return ERROR;
    }

    radListRemove (&id->fields, (NODE_PTR)field);
    if (field->type & FIELD_STRING || field->type == FIELD_DATETIME)
    {
        free (field->cvalue);
    }

    free (field);

    return OK;
}

void raddatabaseRowDescriptionDelete
(
    ROW_ID          row
)
{
    FIELD_ID    field;

    for (field = (FIELD_ID) radListGetFirst (&row->fields);
         field != NULL;
         field = (FIELD_ID) radListGetFirst (&row->fields))
    {
        if ((field->type & FIELD_STRING) || (field->type & FIELD_DATETIME))
        {
            free (field->cvalue);
        }

        radListRemove (&row->fields, (NODE_PTR)field);
        free (field);
    }

    free (row);
}


/*************************** Field - Level ********************************
 **************************************************************************
*/

/*  ... get the field of interest; returns FIELD_ID or NULL
*/
FIELD_ID raddatabaseFieldGet
(
    ROW_ID          id,
    const char      *fieldName
)
{
    FIELD_ID        field;

    for (field = (FIELD_ID) radListGetFirst (&id->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&id->fields, (NODE_PTR)field))
    {
        if (!strcasecmp (field->name, fieldName))
        {
            return field;
        }
    }

    return NULL;
}

/*  ... field extractions; if FIELD_ID is bogus, these will blow chunks!
*/
UINT raddatabaseFieldGetType
(
    FIELD_ID          id
)
{
    return id->type;
}

int raddatabaseFieldGetIntValue
(
    FIELD_ID          id
)
{
    return id->ivalue;
}

long long raddatabaseFieldGetBigIntValue
(
    FIELD_ID          id
)
{
    return id->llvalue;
}

float raddatabaseFieldGetFloatValue
(
    FIELD_ID          id
)
{
    return id->fvalue;
}

double raddatabaseFieldGetDoubleValue
(
    FIELD_ID          id
)
{
    return id->dvalue;
}

char *raddatabaseFieldGetTimeDateValue
(
    FIELD_ID          id
)
{
    return id->cvalue;
}

char *raddatabaseFieldGetCharValue
(
    FIELD_ID          id
)
{
    return id->cvalue;
}

int raddatabaseFieldGetCharLength
(
    FIELD_ID          id
)
{
    return id->cvalLength;
}

/*  ... does not overwrite traits flags
*/
int raddatabaseFieldSetTypeInt
(
    FIELD_ID        id
)
{
    id->type &= FIELD_TYPE_CLEAR;
    id->type |= FIELD_INT;
    return OK;
}

int raddatabaseFieldSetTypeBigInt
(
    FIELD_ID        id
)
{
    id->type &= FIELD_TYPE_CLEAR;
    id->type |= FIELD_BIGINT;
    return OK;
}

int raddatabaseFieldSetTypeFloat
(
    FIELD_ID        id
)
{
    id->type &= FIELD_TYPE_CLEAR;
    id->type |= FIELD_FLOAT;
    return OK;
}

int raddatabaseFieldSetTypeDouble
(
    FIELD_ID        id
)
{
    id->type &= FIELD_TYPE_CLEAR;
    id->type |= FIELD_DOUBLE;
    return OK;
}

int raddatabaseFieldSetTypeDateTime
(
    FIELD_ID        id
)
{
    id->type &= FIELD_TYPE_CLEAR;
    id->type |= FIELD_DATETIME;
    return OK;
}

int raddatabaseFieldSetTypeChar
(
    FIELD_ID        id
)
{
    id->type &= FIELD_TYPE_CLEAR;
    id->type |= FIELD_STRING;
    return OK;
}


int raddatabaseFieldSetToNull
(
    FIELD_ID        id
)
{
    id->type |= FIELD_VALUE_IS_NULL;

    return OK;
}


int raddatabaseFieldSetToNotNull
(
    FIELD_ID        id
)
{
    id->type &= ~FIELD_VALUE_IS_NULL;

    return OK;
}


int raddatabaseFieldSetIntValue
(
    FIELD_ID        id,
    int             value
)
{
    id->ivalue = value;
    id->type &= ~FIELD_VALUE_IS_NULL;
    raddatabaseFieldSetTypeInt (id);

    return OK;
}

int raddatabaseFieldSetBigIntValue
(
    FIELD_ID        id,
    long long       value
)
{
    id->llvalue = value;
    id->type &= ~FIELD_VALUE_IS_NULL;
    raddatabaseFieldSetTypeBigInt (id);
    return OK;
}

int raddatabaseFieldSetFloatValue
(
    FIELD_ID        id,
    float           value
)
{
    id->fvalue = value;
    id->type &= ~FIELD_VALUE_IS_NULL;
    raddatabaseFieldSetTypeFloat (id);
    return OK;
}

int raddatabaseFieldSetDoubleValue
(
    FIELD_ID        id,
    double          value
)
{
    id->dvalue = value;
    id->type &= ~FIELD_VALUE_IS_NULL;
    raddatabaseFieldSetTypeDouble (id);
    return OK;
}

int raddatabaseFieldSetDateTimeValue
(
    FIELD_ID        id,
    const char      *value,
    int             valueLength
)
{
    char            *temp;

    if (id->cvalLength < valueLength)
    {
        temp = (char *) malloc (valueLength+1);
        if (temp == NULL)
        {
            radMsgLog(PRI_MEDIUM, "raddatabaseFieldSetDateTimeValue: malloc failed!");
            return ERROR;
        }

        free (id->cvalue);
        id->cvalue = temp;
    }

    strncpy (id->cvalue, value, valueLength+1);
    id->cvalLength = valueLength;
    id->type &= ~FIELD_VALUE_IS_NULL;
    raddatabaseFieldSetTypeDateTime (id);

    return OK;
}

/*  ... sets both value AND length
*/
int raddatabaseFieldSetCharValue
(
    FIELD_ID        id,
    const char      *value,
    int             valueLength
)
{
    char            *temp;

    if (id->cvalLength < valueLength)
    {
        temp = (char *) malloc (valueLength+1);
        if (temp == NULL)
        {
            radMsgLog(PRI_MEDIUM, "raddatabaseFieldSetCharValue: malloc failed!");
            return ERROR;
        }

        free (id->cvalue);
        id->cvalue = temp;
    }

    strncpy (id->cvalue, value, valueLength+1);
    id->cvalLength = valueLength;
    id->type &= ~FIELD_VALUE_IS_NULL;
    raddatabaseFieldSetTypeChar (id);

    return OK;
}
