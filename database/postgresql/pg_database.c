/*-----------------------------------------------------------------------
 
  FILENAME:
        pg_database.c
 
  PURPOSE:
        Provide the database I/F utilities - postgresql-specific.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Function
        06/06/2005      M.S. Teel       0               Original
 
  ASSUMPTIONS:
        This is linked with libpq ...
 
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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>


/*  ... Library include files
*/
#include <libpq-fe.h>
#include <radlist.h>
#include <radmsgLog.h>
#include <raddatabase.h>

/*  ... Local include files
*/
#include "_pg-types.h"

/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/

/*  ... define the postgresql-specific work area we return as "DATABASE_ID"
*/
typedef struct
{
    PGconn          *dbConn;
    RESULT_SET_ID   resSet;
} *PGRESQL_ID;


/* ... methods
*/
static char errorString[512];
static char *printError (PGRESQL_ID id)
{
    if (id != NULL)
    {
        sprintf (errorString, "error: %s",
                 PQerrorMessage (id->dbConn));
    }
    else
    {
        errorString[0] = 0;
    }

    return errorString;
}

static void freeRow (ROW_ID id)
{
    FIELD_ID        node;

    for (node = (FIELD_ID) radListGetFirst (&id->fields);
         node != NULL;
         node = (FIELD_ID) radListGetFirst (&id->fields))
    {
        if ((node->type & FIELD_STRING) || (node->type & FIELD_DATETIME))
        {
            free (node->cvalue);
        }
        radListRemove (&id->fields, (NODE_PTR)node);
        free (node);
    }

    free (id);
    return;
}

static int processResults (PGRESQL_ID id, PGresult *set)
{
    UINT            row, col, numRows;
    ROW_ID          genRow;
    FIELD_ID        genField;
    int             fieldLength, fieldType;
    char            *fieldVal;
    
    // get the number of rows returned
    numRows = PQntuples (set);
    
    for (row = 0; row < numRows; row ++)
    {
        /*  ... get generic row memory
        */
        genRow = (ROW_ID) malloc (sizeof (*genRow));
        if (genRow == NULL)
        {
            radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: malloc failed!");
            return ERROR;
        }
        memset (genRow, 0, sizeof (*genRow));
        radListReset (&genRow->fields);

        for (col = 0; col < PQnfields (set); col ++)
        {
            if (PQfformat (set, col) != 0)
            {
                radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: binary format returned!");
                freeRow (genRow);
                return ERROR;
            }
            fieldLength = PQgetlength (set, row, col);
            fieldType   = PQftype (set, col);
            fieldVal    = PQgetvalue (set, row, col);

            /*  ... create the generic field memory
            */
            genField = (FIELD_ID) malloc (sizeof (*genField));
            if (genField == NULL)
            {
                radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: malloc failed!");
                raddatabaseRowDescriptionDelete (genRow);
                return ERROR;
            }
            memset (genField, 0, sizeof (*genField));

            strncpy (genField->name, PQfname (set, col), DB_FIELD_NAME_MAX);

            if (PQgetisnull (set, row, col))
            {
                genField->type = FIELD_VALUE_IS_NULL;
                if (fieldType == FIELD_TYPE_TINY || 
                    fieldType == FIELD_TYPE_SHORT ||
                    fieldType == FIELD_TYPE_INT)
                {
                    genField->type |= FIELD_INT;
                }
                else if (fieldType == FIELD_TYPE_LONGLONG)
                {
                    genField->type |= FIELD_BIGINT;
                }
                else if (fieldType == FIELD_TYPE_DATETIME)
                {
                    genField->type |= FIELD_DATETIME;
                }
                else if (fieldType == FIELD_TYPE_FLOAT)
                {
                    genField->type |= FIELD_FLOAT;
                }
                else if (fieldType == FIELD_TYPE_DOUBLE)
                {
                    genField->type |= FIELD_DOUBLE;
                }
                else if (fieldType == FIELD_TYPE_STRING)
                {
                    genField->type |= FIELD_STRING;
                }
                else
                {
                    // this should happen on the first row of the result set,
                    // thus we can safely cleanup the current row only
                    radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: results unknown field type");
                    free (genField);
                    freeRow (genRow);
                    return ERROR;
                }
                
                radListAddToEnd (&genRow->fields, (NODE_PTR)genField);
                continue;
            }

            if (fieldType == FIELD_TYPE_TINY)
            {
                genField->type |= FIELD_INT;
                genField->ivalue = atoi (fieldVal);
            }
            else if (fieldType == FIELD_TYPE_SHORT)
            {
                genField->type |= FIELD_INT;
                genField->ivalue = atoi (fieldVal);
            }
            else if (fieldType == FIELD_TYPE_INT)
            {
                genField->type |= FIELD_INT;
                genField->ivalue = atoi (fieldVal);
            }
            else if (fieldType == FIELD_TYPE_LONGLONG)
            {
                genField->type |= FIELD_BIGINT;
                genField->llvalue = atoll (fieldVal);
            }
            else if (fieldType == FIELD_TYPE_FLOAT)
            {
                genField->type |= FIELD_FLOAT;
                genField->fvalue = (float)atof (fieldVal);
            }
            else if (fieldType == FIELD_TYPE_DOUBLE)
            {
                genField->type |= FIELD_DOUBLE;
                genField->dvalue = atof (fieldVal);
            }
            else if (fieldType == FIELD_TYPE_DATETIME)
            {
                genField->type |= FIELD_DATETIME;
                genField->cvalue = malloc (fieldLength+1);
                if (genField->cvalue == NULL)
                {
                    radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: malloc failed!");
                    freeRow (genRow);
                    free (genField);
                    return ERROR;
                }
                memcpy (genField->cvalue, fieldVal, fieldLength);
                genField->cvalue[fieldLength] = 0;
                genField->cvalLength = fieldLength;
            }
            else if (fieldType == FIELD_TYPE_STRING)
            {
                genField->type |= FIELD_STRING;
                genField->cvalue = malloc (fieldLength+1);
                if (genField->cvalue == NULL)
                {
                    radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: malloc failed!");
                    freeRow (genRow);
                    free (genField);
                    return ERROR;
                }
                memcpy (genField->cvalue, fieldVal, fieldLength);
                genField->cvalue[fieldLength] = 0;
                genField->cvalLength = fieldLength;
            }
            else
            {
                // this should happen on the first row of the result set,
                // thus we can safely cleanup the current row only
                radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: results unknown field type");
                free (genField);
                freeRow (genRow);
                return ERROR;
            }
            

            /*  ... add to the generic field list here...
            */
            radListAddToEnd (&genRow->fields, (NODE_PTR)genField);
        }

        /*  ... add to the generic result row list here ...
        */
        radListAddToEnd (&id->resSet->rows, (NODE_PTR)genRow);
    }

    return OK;
}


/************************** Database - Level ******************************
 **************************************************************************
*/
/*  ... connect to a database server;
    ... if 'host' is NULL, localhost is used;
    ... 'username' and 'password' MUST be populated and are the db 
    ... server username and password;
    ... if 'dbName' is NULL, no specific database is opened and all
    ... tableNames in calls below MUST be of the form 'dbName.tableName';
    ... returns the ID or NULL
*/
DATABASE_ID raddatabaseOpen
(
    const char  *host,
    const char  *username,
    const char  *password,
    const char  *dbName
)
{
    PGRESQL_ID  newId;
    char        conninfo[256], hoststr[64];
    
    if (dbName == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: postgresql requires a non-NULL dbName!");
        return NULL;
    }
    if (username == NULL || password == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: non-NULL username and password required!");
        return NULL;
    }
    
    
    if (host == NULL)
    {
        sprintf (hoststr, "host=localhost");
    }
    else
    {
        if (isdigit(host[0]))
        {
            sprintf (hoststr, "hostaddr=%s", host);
        }
        else
        {
            sprintf (hoststr, "host=%s", host);
        }
    }

    newId = (PGRESQL_ID) malloc (sizeof *newId);
    if (newId == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: malloc failed!");
        return NULL;
    }

    memset (newId, 0, sizeof (*newId));

    /*  ...allocate, initialize connection handler
    */
    sprintf (conninfo, "%s dbname=%s user=%s password=%s",
             hoststr, dbName, username, password);

    newId->dbConn = PQconnectdb (conninfo);
    if (newId->dbConn == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: "
                "PQconnectdb() failed (probably out of memory)");
        free (newId);
        return NULL;
    }
    else if (PQstatus (newId->dbConn) != CONNECTION_OK)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: "
                "PQstatus(): %s", printError (newId));
        PQfinish (newId->dbConn);
        free (newId);
        return NULL;
    }

    return (DATABASE_ID)newId;
}


/*  ... close a database server connection
*/
void raddatabaseClose (DATABASE_ID id)
{
    PGRESQL_ID        pgId = (PGRESQL_ID)id;

    PQfinish (pgId->dbConn);

    if (pgId->resSet)
    {
        raddatabaseReleaseResults (id, pgId->resSet);
    }

    free (pgId);

    return;
}

/*  ... create a database on the db server;
    ... returns OK or ERROR
*/
int raddatabaseCreate
(
    DATABASE_ID     id,
    const char      *dbName
)
{
    char            query[DB_QUERY_LENGTH_MAX];

    sprintf (query, "CREATE DATABASE %s", dbName);

    return (raddatabaseQuery (id, query, FALSE));
}

/*  ... destroy a database on the db server;
    ... returns OK or ERROR
*/
int raddatabaseDelete
(
    DATABASE_ID     id,
    const char      *dbName
)
{
    char            query[DB_QUERY_LENGTH_MAX];

    sprintf (query, "DROP DATABASE %s", dbName);

    return (raddatabaseQuery (id, query, FALSE));
}


/*  ... issue an SQL query to the db engine;
    ... 'createResults' should be set to TRUE if a result set should
    ... be created for retrieval with the raddatabaseTableGetResults function
    ... described below, otherwise set 'createResults' to FALSE;
    ... if results are generated it can be VERY slow and eat up a lot of 
    ... heap space; the good news is that once the RESULT_SET_ID is 
    ... retrieved, it can persist as long as the user needs to keep it 
    ... without adverse effects on the db server;
    ... returns OK or ERROR if there is a db server error, query error,
    ... or 'createResults' is set to TRUE and no result set is generated 
    ... by the 'query'
*/
int raddatabaseQuery
(
    DATABASE_ID     id,
    const char      *query,
    int             createResults
)
{
    PGRESQL_ID      pgId = (PGRESQL_ID)id;
    PGresult        *queryResult;
    ExecStatusType  retVal;

    if (strlen (query) > DB_QUERY_LENGTH_MAX-1)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: "
                "query string longer than %d characters...",
                DB_QUERY_LENGTH_MAX-1);
        return ERROR;
    }

    pgId->resSet = NULL;

    queryResult = PQexec (pgId->dbConn, query);
    if (queryResult == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: PQexec(): %s", 
                   printError (pgId));
        return ERROR;
    }
    
    retVal = PQresultStatus (queryResult);
    if (retVal == PGRES_FATAL_ERROR || 
        retVal == PGRES_BAD_RESPONSE ||
        retVal == PGRES_EMPTY_QUERY)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: PQexec(): %s", 
                   PQresultErrorMessage (queryResult));
        PQclear (queryResult);
        return ERROR;
    }

    /* the query succeeded; determine whether or not it returns data */
    if (retVal == PGRES_COMMAND_OK)
    {
        if (createResults)
        {
            /*
            * no result set was returned; query returned no data
            * (it was not a SELECT, SHOW, DESCRIBE, or EXPLAIN),
            * so just report number of rows affected by query
            */
            radMsgLog(PRI_STATUS, "raddatabaseTableQuery:"
                       " no result set for this query");
            PQclear (queryResult);
            return ERROR;
        }
        else
        {
            PQclear (queryResult);
            return OK;
        }
    }

    if (retVal != PGRES_TUPLES_OK || !createResults)
    {
        PQclear (queryResult);
        return OK;
    }


    /*  ... a result set was returned (and we want one!)
        ... store rows in the generic data structure
    */
    pgId->resSet = (RESULT_SET_ID) malloc (sizeof (*(pgId->resSet)));

    if (pgId->resSet == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery:"
                   " malloc failed!");
        PQclear (queryResult);
        return ERROR;
    }

    memset (pgId->resSet, 0, sizeof (*(pgId->resSet)));
    radListReset (&pgId->resSet->rows);

    strncpy (pgId->resSet->query, query, DB_QUERY_LENGTH_MAX-1);

    if (processResults (pgId, queryResult) == ERROR)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery:"
                " processResults failed!");

        raddatabaseReleaseResults (id, pgId->resSet);
        pgId->resSet = NULL;
        PQclear (queryResult);
        return ERROR;
    }

    PQclear (queryResult);
    return OK;
}


/*  ... retrieve the result set if there is one;
    ... should be called immediately after raddatabaseTableQuery if the
    ... query was supposed to generate a result set (SELECT, SHOW, etc.);
    ... returns NULL if there is no result set available;
    ... RESULT_SET_ID should be released via raddatabaseTableResultsRelease
    ... once the user is finished with it;
    ... returns RESULT_SET_ID or NULL
*/
RESULT_SET_ID raddatabaseGetResults
(
    DATABASE_ID     id
)
{
    PGRESQL_ID      pgId = (PGRESQL_ID)id;

    return pgId->resSet;
}


/*  ... refresh and replace a result set based on the original query;
    ... can be even SLOWER;
    ... RESULT_SET_ID should be released via raddatabaseTableResultsRelease
    ... once the user is finished with it;
    ... returns a new RESULT_SET_ID or NULL
*/
RESULT_SET_ID raddatabaseRefreshResults
(
    DATABASE_ID     id,
    RESULT_SET_ID   resSetId
)
{
    char            query[DB_QUERY_LENGTH_MAX];
    RESULT_SET_ID   newSetId;

    strncpy (query, resSetId->query, DB_QUERY_LENGTH_MAX-1);

    raddatabaseReleaseResults (id, resSetId);

    if (raddatabaseQuery (id, query, TRUE) == ERROR)
    {
        return NULL;
    }

    if ((newSetId = raddatabaseGetResults (id)) == NULL)
    {
        return NULL;
    }

    return newSetId;
}


/*  ... release a result set
*/
void raddatabaseReleaseResults
(
    DATABASE_ID     id,
    RESULT_SET_ID   resSet
)
{
    PGRESQL_ID      pgId = (PGRESQL_ID)id;
    ROW_ID          row;

    for (row = (ROW_ID) radListGetFirst (&resSet->rows);
         row != NULL;
         row = (ROW_ID) radListGetFirst (&resSet->rows))
    {
        radListRemove (&resSet->rows, (NODE_PTR)row);
        raddatabaseRowDescriptionDelete (row);
    }

    pgId->resSet = NULL;
    free (resSet);
    return;
}


/*************************** Table - Level ********************************
 **************************************************************************
*/

/*  ... does 'tableName' table exist?;
    ... returns TRUE or FALSE
*/
int raddatabaseTableIfExists
(
    DATABASE_ID     id,
    const char      *tableName
)
{
    PGRESQL_ID      pgId = (PGRESQL_ID)id;
    PGresult        *queryResult;
    ExecStatusType  retVal;
    char            query[DB_QUERY_LENGTH_MAX];
    int             boolReturn = FALSE;

    sprintf (query, "SELECT relname FROM pg_class WHERE relname='%s'", tableName);

    queryResult = PQexec (pgId->dbConn, query);
    if (queryResult == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableIfExists: PQexec(): %s", 
                   printError (pgId));
        return FALSE;
    }
    
    retVal = PQresultStatus (queryResult);
    if (retVal == PGRES_FATAL_ERROR || 
        retVal == PGRES_BAD_RESPONSE ||
        retVal == PGRES_EMPTY_QUERY)
    {
        PQclear (queryResult);
        return FALSE;
    }

    /* the query succeeded; determine whether or not it returns data */
    if ((retVal == PGRES_TUPLES_OK) && (PQntuples(queryResult) > 0))
    {
        boolReturn = TRUE;
    }

    PQclear (queryResult);
    return boolReturn;
}


/*  ... table management; all return OK or ERROR
*/
int raddatabaseTableCreate
(
    DATABASE_ID     id,
    const char      *tableName,
    ROW_ID          rowDescr        /* see raddatabaseRowDescriptionCreate */
)
{
    char            query[DB_QUERY_LENGTH_MAX];
    char            fType[12], notNull[12];
    int             index = 0, priKey = FALSE;
    FIELD_ID        field;

    index = sprintf (query, "CREATE TABLE %s ( ", tableName);

    /*  ... do the column definitions first
    */
    for (field = (FIELD_ID) radListGetFirst (&rowDescr->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->name[0] == 0)
        {
            radMsgLog(PRI_MEDIUM, "raddatabaseTableCreate: field name is empty!");
            return ERROR;
        }
        if (field->type == 0)
        {
            radMsgLog(PRI_MEDIUM, "raddatabaseTableCreate: type is empty!");
            return ERROR;
        }
        if ((field->type & FIELD_STRING) && (field->cvalLength == 0))
        {
            radMsgLog(PRI_MEDIUM, "raddatabaseTableCreate: cval length is 0!");
            return ERROR;
        }

        if (field->type & FIELD_INT)
        {
            sprintf (fType, "int4");
        }
        else if (field->type & FIELD_BIGINT)
        {
            sprintf (fType, "int8");
        }
        else if (field->type & FIELD_FLOAT)
        {
            sprintf (fType, "float4");
        }
        else if (field->type & FIELD_DOUBLE)
        {
            sprintf (fType, "float8");
        }
        else if (field->type & FIELD_DATETIME)
        {
            sprintf (fType, "timestamp");
        }
        else
        {
            sprintf (fType, "char(%d)", field->cvalLength);
        }

        if (field->type & FIELD_NOT_NULL)
        {
            sprintf (notNull, "NOT NULL");
        }
        else
        {
            notNull[0] = 0;
        }

        index += sprintf (&query[index], "%s %s %s,",
                          field->name, fType, notNull);
    }

    /*  ... now do the indexes
    */
    for (field = (FIELD_ID) radListGetFirst (&rowDescr->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->type & FIELD_PRI_KEY)
        {
            if (priKey)
            {
                radMsgLog(PRI_MEDIUM, "raddatabaseTableCreate: "
                        "more than one PRIMARY KEY specified!");
                return ERROR;
            }

            index += sprintf (&query[index], "PRIMARY KEY (%s),",
                              field->name);
            priKey = TRUE;
            continue;
        }
        else if (field->type & FIELD_UNIQUE_INDEX)
        {
            index += sprintf (&query[index], "UNIQUE (%s),",
                              field->name);
            continue;
        }
    }

    if (query[index-1] == ',')
    {
        index --;
    }

    sprintf (&query[index], ")");

    return (raddatabaseQuery (id, query, FALSE));
}

int raddatabaseTableDelete
(
    DATABASE_ID     id,
    const char      *tableName
)
{
    char            query[DB_QUERY_LENGTH_MAX];

    sprintf (query, "DROP TABLE %s", tableName);

    return (raddatabaseQuery (id, query, FALSE));
}

int raddatabaseTableTruncate
(
    DATABASE_ID     id,
    const char      *tableName
)
{
    char            query[DB_QUERY_LENGTH_MAX];

    sprintf (query, "DELETE FROM %s", tableName);

    return (raddatabaseQuery (id, query, FALSE));
}


/*  ... create a row description to use for row insertion/retrieval;
    ... must be deleted with raddatabaseRowDescriptionDelete after use;
    ... sets the FIELD_VALUE_IS_NULL bit in field->type for all fields -
    ... this means the user must clear this bit for field values to be 
    ... used in queries/insertions/deletions;
    ... returns ROW_ID or NULL
*/
ROW_ID raddatabaseTableDescriptionGet
(
    DATABASE_ID     id,
    const char      *tableName
)
{
    ROW_ID          newRow;
    PGRESQL_ID      pgId = (PGRESQL_ID)id;
    char            query[DB_QUERY_LENGTH_MAX];
    int             j, numRows, row, col, type;
    FIELD_ID        genField;
    PGresult        *queryResult;
    ExecStatusType  retVal;
    char            *fieldName, *fieldVal;

    newRow = (ROW_ID) malloc (sizeof (*newRow));
    if (newRow == NULL)
    {
        radMsgLog(PRI_STATUS, "raddatabaseTableDescriptionGet: malloc failed!");
        return NULL;
    }
    memset (newRow, 0, sizeof (*newRow));
    radListReset (&newRow->fields);

    sprintf (query, 
             "SELECT attname,atttypid,attnotnull from pg_class, pg_attribute "
             "WHERE relname='%s' AND pg_class.oid=attrelid AND attnum > 0", 
             tableName);
    
    pgId->resSet = NULL;

    queryResult = PQexec (pgId->dbConn, query);
    if (queryResult == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableDescriptionGet: PQexec(): %s", 
                   printError (pgId));
        return NULL;
    }
    
    retVal = PQresultStatus (queryResult);
    if (retVal == PGRES_FATAL_ERROR || 
        retVal == PGRES_BAD_RESPONSE ||
        retVal == PGRES_EMPTY_QUERY)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableDescriptionGet: PQexec(): %s", 
                   PQresultErrorMessage (queryResult));
        PQclear (queryResult);
        return NULL;
    }

    /* the query succeeded; determine whether or not it returns data */
    if (retVal != PGRES_TUPLES_OK)
    {
        /*
         * no result set was returned; query returned no data
         * (it was not a SELECT, SHOW, DESCRIBE, or EXPLAIN),
         * so just report number of rows affected by query
         */
        radMsgLog(PRI_STATUS, "raddatabaseTableDescriptionGet:"
                   " table is empty!");

        PQclear (queryResult);
        free (newRow);
        return NULL;
    }


    numRows = PQntuples (queryResult);
    if (numRows <= 0) /* no result set was returned */
    {
        /*
         * no result set was returned; query returned no data
         * (it was not a SELECT, SHOW, DESCRIBE, or EXPLAIN),
         * so just report number of rows affected by query
         */
        radMsgLog(PRI_STATUS, "raddatabaseTableDescriptionGet:"
                   " table is empty!");

        PQclear (queryResult);
        free (newRow);
        return NULL;
    }

    // there should be one row per column to process...
    for (row = 0; row < numRows; row ++)
    {
        /*  ... get generic field memory
        */
        genField = (FIELD_ID) malloc (sizeof (*genField));
        if (genField == NULL)
        {
            radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableDescriptionGet: "
                       "malloc failed!");
            PQclear (queryResult);
            freeRow (newRow);
            return NULL;
        }
        memset (genField, 0, sizeof (*genField));

        for (col = 0; col < PQnfields (queryResult); col ++)
        {
            if (PQfformat (queryResult, col) != 0)
            {
                radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableDescriptionGet: "
                           "binary format returned!");
                PQclear (queryResult);
                freeRow (newRow);
                return NULL;
            }
            
            fieldName   = PQfname (queryResult, col);
            fieldVal    = PQgetvalue (queryResult, row, col);

            if (!strcasecmp (fieldName, "attname"))
            {
                strncpy (genField->name, fieldVal, DB_FIELD_NAME_MAX);
            }
            else if (!strcasecmp (fieldName, "atttypid"))
            {
                type = atoi (fieldVal);
                switch (type)
                {
                    case FIELD_TYPE_TINY:
                    case FIELD_TYPE_SHORT:
                    case FIELD_TYPE_INT:
                        genField->type = FIELD_INT;
                        break;

                    case FIELD_TYPE_LONGLONG:
                        genField->type = FIELD_BIGINT;
                        break;

                    case FIELD_TYPE_FLOAT:
                        genField->type = FIELD_FLOAT;
                        break;

                    case FIELD_TYPE_DOUBLE:
                        genField->type = FIELD_DOUBLE;
                        break;

                    case FIELD_TYPE_STRING:
                        genField->type = FIELD_STRING;;
                        break;

                    case FIELD_TYPE_DATETIME:
                        genField->type = FIELD_DATETIME;;
                        break;

                    default:
                        continue;
                }
                
                /*  ... set to be unused by default
                */
                genField->type |= FIELD_VALUE_IS_NULL;
            }
            else if (!strcasecmp (fieldName, "attnotnull"))
            {
                if (fieldVal[0] == 't')
                {
                    genField->type |= FIELD_NOT_NULL;
                }
            }
            else
            {
                continue;
            }
        }

        /*  ... add to the generic field list here...
        */
        radListAddToEnd (&newRow->fields, (NODE_PTR)genField);
    }

    PQclear (queryResult);
    return newRow;
}


/*  ... query a table to create a result set given a row description;
    ... columns to be included in the result set should have FIELD_DISPLAY
    ... set in the appropriate FIELD_ID.type within 'rowDescription';
    ... columns NOT to be included in the WHERE clause should have
    ... FIELD_VALUE_IS_NULL set in FIELD_ID.type;
    ... can be VERY slow and eat up a lot of heap space; the good news 
    ... is that once the RESULT_SET_ID is retrieved, it can persist as 
    ... long as the user needs to keep it without adverse effects on 
    ... the db server;
    ... RESULT_SET_ID should be released via raddatabaseTableResultsRelease
    ... once the user is finished with it;
    ... only the populated fields in 'rowDescription' will be used to 
    ... match records in the table (i.e., multiple rows can be matched)
    ... returns RESULT_SET_ID or NULL
*/
RESULT_SET_ID raddatabaseTableQueryRow
(
    DATABASE_ID     id,
    const char      *tableName,
    ROW_ID          rowDescr
)
{
    char            query[DB_QUERY_LENGTH_MAX];
    char            select[DB_QUERY_LENGTH_MAX];
    char            where[DB_QUERY_LENGTH_MAX];
    int             index, firstWhere = TRUE;
    FIELD_ID        field;

    /*  ... build the select clause
    */
    index = 0;
    for (field = (FIELD_ID) radListGetFirst (&rowDescr->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->type & FIELD_DISPLAY)
        {
            index += sprintf (&select[index], "%s,", field->name);
        }
    }
    select[index-1] = 0;

    /*  ... build the where clause
    */
    index = 0;
    for (field = (FIELD_ID) radListGetFirst (&rowDescr->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->type & FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (!firstWhere)
        {
            index += sprintf (&where[index], "AND ");
        }
        firstWhere = FALSE;

        if (field->type & FIELD_INT)
        {
            index += sprintf (&where[index], "(%s = %d)",
                              field->name, field->ivalue);
        }
        else if (field->type & FIELD_BIGINT)
        {
            index += sprintf (&where[index], "(%s = %lld)",
                              field->name, field->llvalue);
        }
        else if (field->type & FIELD_FLOAT)
        {
            index += sprintf (&where[index], "(%s = %f)",
                              field->name, field->fvalue);
        }
        else if (field->type & FIELD_DOUBLE)
        {
            index += sprintf (&where[index], "(%s = %f)",
                              field->name, field->dvalue);
        }
        else
        {
            index += sprintf (&where[index], "(%s = \"%s\")",
                              field->name, field->cvalue);
        }
    }

    /*  ... build the query
    */
    sprintf (query, "SELECT %s FROM %s WHERE %s",
             select, tableName, where);

    /*  ... now we have a query, call the other guy
    */
    if (raddatabaseQuery (id, query, TRUE) == ERROR)
    {
        return NULL;
    }

    return (raddatabaseGetResults (id));
}


/*  ... insert a row into a table;
    ... 'rowId' was created with raddatabaseTableDescriptionGet then
    ... field values were populated with raddatabaseFieldGet, 
    ... raddatabaseFieldSetIntValue, etc. prior to this call;
    ... if a 'NOT NULL' field has a NULL value in rowId, it's an error;
    ... returns OK or ERROR
*/
int raddatabaseTableInsertRow
(
    DATABASE_ID     id,
    const char      *tableName,
    ROW_ID          rowId
)
{
    FIELD_ID        field;
    char            query[DB_QUERY_LENGTH_MAX];
    char            columns[DB_QUERY_LENGTH_MAX];
    char            values[DB_QUERY_LENGTH_MAX];
    int             colindex, valindex;

    sprintf (query, "INSERT INTO %s ", tableName);
    colindex = sprintf (columns, "(");
    valindex = sprintf (values, " VALUES (");
    

    /*  ... build the columns and values strings
    */
    for (field = (FIELD_ID) radListGetFirst (&rowId->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&rowId->fields, (NODE_PTR)field))
    {
        /*      ... is this a NOT NULL field with NULL value?
        */
        if ((field->type & FIELD_NOT_NULL) &&
                (field->type & FIELD_VALUE_IS_NULL))
        {
            radMsgLog(PRI_MEDIUM, "raddatabaseTableInsertRow: "
                    "NOT NULL field has NULL value!");
            return ERROR;
        }

        if (field->type & FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (field->type & FIELD_INT)
        {
            colindex += sprintf (&columns[colindex], "%s,",
                                 field->name);
            valindex += sprintf (&values[valindex], "%d,",
                                 field->ivalue);
        }
        else if (field->type & FIELD_BIGINT)
        {
            colindex += sprintf (&columns[colindex], "%s,",
                                 field->name);
            valindex += sprintf (&values[valindex], "%lld,",
                                 field->llvalue);
        }
        else if (field->type & FIELD_FLOAT)
        {
            colindex += sprintf (&columns[colindex], "%s,",
                                 field->name);
            valindex += sprintf (&values[valindex], "%f,",
                                 field->fvalue);
        }
        else if (field->type & FIELD_DOUBLE)
        {
            colindex += sprintf (&columns[colindex], "%s,",
                                 field->name);
            valindex += sprintf (&values[valindex], "%f,",
                                 field->dvalue);
        }
        else
        {
            colindex += sprintf (&columns[colindex], "%s,",
                                 field->name);
            valindex += sprintf (&values[valindex], "'%s',",
                                 field->cvalue);
        }
    }

    /*  ... get rid of the trailing ','s
    */
    columns[colindex-1] = ')';
    values[valindex-1] = ')';
    
    strcat (query, columns);
    strcat (query, values);
    
    return (raddatabaseQuery (id, query, FALSE));
}


/*  ... modify rows in a table matching 'matchId';
    ... only the non-NULL fields will be used to match records
    ... in the table (i.e., multiple rows can be matched)
    ... all fields in newData will be updated in the db rows - if a NULL
    ... value is given for a NOT NULL field it is an error;
    ... thus if you don't want to modify a column value, remove it from
    ... the newData row desription;
    ... returns OK or ERROR
*/
int raddatabaseTableModifyRows
(
    DATABASE_ID     id,
    const char      *tableName,
    ROW_ID          matchId,
    ROW_ID          newData
)
{
    char            query[DB_QUERY_LENGTH_MAX];
    char            set[DB_QUERY_LENGTH_MAX];
    char            where[DB_QUERY_LENGTH_MAX];
    int             index, firstWhere = TRUE;
    FIELD_ID        field;

    /*  ... build the set clause
    */
    index = 0;
    for (field = (FIELD_ID) radListGetFirst (&newData->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&newData->fields, (NODE_PTR)field))
    {
        /*      ... is this a NOT NULL field with NULL value?
        */
        if ((field->type & FIELD_NOT_NULL) &&
                (field->type & FIELD_VALUE_IS_NULL))
        {
            radMsgLog(PRI_MEDIUM, "raddatabaseTableModifyRows: "
                    "NOT NULL field has NULL value!");
            return ERROR;
        }
        if (field->type & FIELD_VALUE_IS_NULL)
        {
            index += sprintf (&set[index], "%s = NULL,",
                              field->name);
        }

        if (field->type & FIELD_INT)
        {
            index += sprintf (&set[index], "%s = %d,",
                              field->name, field->ivalue);
        }
        else if (field->type & FIELD_BIGINT)
        {
            index += sprintf (&set[index], "%s = %lld,",
                              field->name, field->llvalue);
        }
        else if (field->type & FIELD_FLOAT)
        {
            index += sprintf (&set[index], "%s = %f,",
                              field->name, field->fvalue);
        }
        else if (field->type & FIELD_DOUBLE)
        {
            index += sprintf (&set[index], "%s = %f,",
                              field->name, field->dvalue);
        }
        else
        {
            index += sprintf (&set[index], "%s = \"%s\",",
                              field->name, field->cvalue);
        }
    }
    set[index-1] = 0;

    /*  ... build the where clause
    */
    index = 0;
    for (field = (FIELD_ID) radListGetFirst (&matchId->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&matchId->fields, (NODE_PTR)field))
    {
        if (field->type & FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (!firstWhere)
        {
            index += sprintf (&where[index], "AND ");
        }
        firstWhere = FALSE;

        if (field->type & FIELD_INT)
        {
            index += sprintf (&where[index], "(%s = %d)",
                              field->name, field->ivalue);
        }
        else
        {
            index += sprintf (&where[index], "(%s = \"%s\")",
                              field->name, field->cvalue);
        }
    }

    /*  ... build the query
    */
    sprintf (query, "UPDATE %s SET %s WHERE %s",
             tableName, set, where);

    /*  ... now we have a query, call the other guy
    */
    return (raddatabaseQuery (id, query, FALSE));
}

/*  ... delete rows in a table matching 'matchId';
    ... only the non-NULL fields will be used to match records
    ... in the table (i.e., multiple rows can be matched)
    ... returns OK or ERROR
*/
int raddatabaseTableDeleteRows
(
    DATABASE_ID     id,
    const char      *tableName,
    ROW_ID          matchId
)
{
    FIELD_ID        field;
    char            query[DB_QUERY_LENGTH_MAX];
    int             index, firstWhere = TRUE;

    index = sprintf (query, "DELETE FROM %s WHERE ", tableName);

    /*  ... build all the "WHERE" clause
    */
    for (field = (FIELD_ID) radListGetFirst (&matchId->fields);
         field != NULL;
         field = (FIELD_ID) radListGetNext (&matchId->fields, (NODE_PTR)field))
    {
        if (field->type & FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (!firstWhere)
        {
            index += sprintf (&query[index], " AND ");
        }
        firstWhere = FALSE;

        if (field->type & FIELD_INT)
        {
            index += sprintf (&query[index], "(%s = %d)",
                              field->name, field->ivalue);
        }
        else if (field->type & FIELD_BIGINT)
        {
            index += sprintf (&query[index], "(%s = %lld)",
                              field->name, field->llvalue);
        }
        else if (field->type & FIELD_FLOAT)
        {
            index += sprintf (&query[index], "(%s = %f)",
                              field->name, field->fvalue);
        }
        else if (field->type & FIELD_DOUBLE)
        {
            index += sprintf (&query[index], "(%s = %f)",
                              field->name, field->dvalue);
        }
        else
        {
            index += sprintf (&query[index], "(%s = \"%s\")",
                              field->name, field->cvalue);
        }
    }

    return (raddatabaseQuery (id, query, FALSE));
}
