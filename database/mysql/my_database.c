/*-----------------------------------------------------------------------
 
  FILENAME:
        my_database.c
 
  PURPOSE:
        Provide the database I/F utilities - MySQL-specific.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Function
        01/02/2002      M.S. Teel       0               Original
 
  ASSUMPTIONS:
        This is linked with libmysqlclient ...
 
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
#include <mysql.h>
#include <radlist.h>
#include <radmsgLog.h>
#include <raddatabase.h>

/*  ... Local include files
*/

/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/

/*  ... define the MySQL-specific work area we return as "DATABASE_ID"
*/
typedef struct
{
    MYSQL           *dbConn;
    RESULT_SET_ID   resSet;
} *MYSQL_ID;


/* ... methods
*/
static char errorString[512];
static char *printError (MYSQL_ID id)
{
    if (id != NULL)
    {
        sprintf (errorString, "error %u (%s)",
                 mysql_errno (id->dbConn), mysql_error (id->dbConn));
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

static int processResults (MYSQL_ID id, MYSQL_RES *set)
{
    MYSQL_FIELD     *field;
    MYSQL_ROW       row;
    UINT            i;
    ROW_ID          genRow;
    FIELD_ID        genField;
    ULONG           *fieldLengths;

    while ((row = mysql_fetch_row (set)) != NULL)
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

        fieldLengths = mysql_fetch_lengths (set);

        mysql_field_seek (set, 0);
        for (i = 0; i < mysql_num_fields (set); i++)
        {
            field = mysql_fetch_field (set);

            /*  ... create the generic field memory
            */
            genField = (FIELD_ID) malloc (sizeof (*genField));
            if (genField == NULL)
            {
                radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: malloc failed!");
                freeRow (genRow);
                return ERROR;
            }
            memset (genField, 0, sizeof (*genField));

            strncpy (genField->name, field->name, DB_FIELD_NAME_MAX);

            if (row[i] == NULL)
            {
                genField->type = FIELD_VALUE_IS_NULL;
                if (field->type >= FIELD_TYPE_TINY && field->type <= FIELD_TYPE_LONG)
                {
                    genField->type |= FIELD_INT;
                }
                else if (field->type == FIELD_TYPE_LONGLONG)
                {
                    genField->type |= FIELD_BIGINT;
                }
                else if (field->type == FIELD_TYPE_DATETIME)
                {
                    genField->type |= FIELD_DATETIME;
                }
                else if (field->type == FIELD_TYPE_FLOAT)
                {
                    genField->type |= FIELD_FLOAT;
                }
                else if (field->type == FIELD_TYPE_DOUBLE)
                {
                    genField->type |= FIELD_DOUBLE;
                }
                else
                {
                    genField->type |= FIELD_STRING;
                }
                radListAddToEnd (&genRow->fields, (NODE_PTR)genField);
                continue;
            }

            if (field->type >= FIELD_TYPE_TINY && field->type <= FIELD_TYPE_LONG)
            {
                genField->type |= FIELD_INT;
                genField->ivalue = atoi (row[i]);
            }
            else if (field->type == FIELD_TYPE_LONGLONG)
            {
                genField->type |= FIELD_BIGINT;
                genField->llvalue = atoll (row[i]);
            }
            else if (field->type == FIELD_TYPE_FLOAT)
            {
                genField->type |= FIELD_FLOAT;
                genField->fvalue = (float)atof (row[i]);
            }
            else if (field->type == FIELD_TYPE_DOUBLE)
            {
                genField->type |= FIELD_DOUBLE;
                genField->dvalue = atof (row[i]);
            }
            else if (field->type == FIELD_TYPE_DATETIME)
            {
                genField->type |= FIELD_DATETIME;
                genField->cvalue = malloc (fieldLengths[i]+1);
                if (genField->cvalue == NULL)
                {
                    radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: malloc failed!");
                    freeRow (genRow);
                    free (genField);
                    return ERROR;
                }
                memcpy (genField->cvalue, row[i], fieldLengths[i]);
                genField->cvalue[fieldLengths[i]] = 0;
                genField->cvalLength = fieldLengths[i];
            }
            else
            {
                // assume character string for everything else
                genField->type |= FIELD_STRING;
                genField->cvalue = malloc (fieldLengths[i]+1);
                if (genField->cvalue == NULL)
                {
                    radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: malloc failed!");
                    freeRow (genRow);
                    free (genField);
                    return ERROR;
                }
                memcpy (genField->cvalue, row[i], fieldLengths[i]);
                genField->cvalue[fieldLengths[i]] = 0;
                genField->cvalLength = fieldLengths[i];
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
    MYSQL_ID        newId;

    newId = (MYSQL_ID) malloc (sizeof *newId);
    if (newId == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: malloc failed!");
        return NULL;
    }

    memset (newId, 0, sizeof (*newId));

    /*  ...allocate, initialize connection handler
    */
    newId->dbConn = mysql_init (NULL);
    if (newId->dbConn == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: "
                "mysql_init() failed (probably out of memory)");
        free (newId);
        return NULL;
    }

#if defined(MYSQL_VERSION_ID) && MYSQL_VERSION_ID >= 32200 /* 3.22 and up */
    if (mysql_real_connect (newId->dbConn,
                            host,
                            username,
                            password,
                            dbName,
                            0,
                            NULL,
                            0)
            == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: mysql_real_connect():"
                " %s", printError (newId));
        free (newId);
        return NULL;
    }

#else       /* pre-3.22 */
    if (mysql_real_connect (newId->dbConn, host, NULL, NULL,
                            0, NULL, 0)
            == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: mysql_real_connect():"
                " %s", printError (newId));
        free (newId);
        return NULL;
    }

    if (dbName != NULL)  /* simulate effect of db_name parameter */
    {
        if (mysql_select_db (newId->dbConn, dbName) != 0)
        {
            radMsgLog(PRI_CATASTROPHIC, "raddatabaseOpen: mysql_select_db():"
                    " %s", printError (newId));
            mysql_close (newId->dbConn);
            free (newId);
            return NULL;
        }
    }
#endif

    return (DATABASE_ID)newId;
}


/*  ... close a database server connection
*/
void raddatabaseClose (DATABASE_ID id)
{
    MYSQL_ID        mysqlId = (MYSQL_ID)id;

    mysql_close (mysqlId->dbConn);

    if (mysqlId->resSet)
    {
        raddatabaseReleaseResults (id, mysqlId->resSet);
    }

    free (mysqlId);

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
    MYSQL_ID        mysqlId = (MYSQL_ID)id;
    MYSQL_RES       *results;

    if (strlen (query) > DB_QUERY_LENGTH_MAX-1)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: "
                "query string longer than %d characters...",
                DB_QUERY_LENGTH_MAX-1);
        return ERROR;
    }

    mysqlId->resSet = NULL;

    if (mysql_query (mysqlId->dbConn, query) != 0) /* the query failed */
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery: mysql_query():"
                " %s", printError (mysqlId));
        return ERROR;
    }

    /* the query succeeded; determine whether or not it returns data */

    results = mysql_store_result (mysqlId->dbConn);
    if (results == NULL) /* no result set was returned */
    {
        /*
         * does the lack of a result set mean that an error
         * occurred or that no result set was returned?
         */
        if (mysql_field_count (mysqlId->dbConn) > 0)
        {
            /*
             * a result set was expected, but mysql_store_result()
             * did not return one; this means an error occurred
             */
            radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery:"
                    " problem processing result set:"
                    " %s", printError (mysqlId));
            return ERROR;
        }
        else
        {
            if (createResults)
            {
                /*
                * no result set was returned; query returned no data
                * (it was not a SELECT, SHOW, DESCRIBE, or EXPLAIN),
                * so just report number of rows affected by query
                */
                radMsgLog(PRI_STATUS, "raddatabaseTableQuery:"
                        " no result set for this query, %d rows affected",
                        mysql_affected_rows (mysqlId->dbConn));
                return ERROR;
            }
            else
            {
                return OK;
            }
        }
    }

    if (!createResults)
    {
        mysql_free_result (results);
        return OK;
    }


    /*  ... a result set was returned (and we want one!)
        ... store rows in the generic data structure
    */
    mysqlId->resSet = (RESULT_SET_ID) malloc (sizeof (*(mysqlId->resSet)));

    if (mysqlId->resSet == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery:"
                " malloc failed!");
        mysql_free_result (results);
        return ERROR;
    }

    memset (mysqlId->resSet, 0, sizeof (*(mysqlId->resSet)));
    radListReset (&mysqlId->resSet->rows);

    strncpy (mysqlId->resSet->query, query, DB_QUERY_LENGTH_MAX-1);

    if (processResults (mysqlId, results) == ERROR)
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableQuery:"
                " processResults failed!");

        raddatabaseReleaseResults (id, mysqlId->resSet);
        mysqlId->resSet = NULL;
        mysql_free_result (results);
        return ERROR;
    }

    mysql_free_result (results);
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
    MYSQL_ID        mysqlId = (MYSQL_ID)id;

    return mysqlId->resSet;
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
    MYSQL_ID        mysqlId = (MYSQL_ID)id;
    ROW_ID          row;

    for (row = (ROW_ID) radListGetFirst (&resSet->rows);
         row != NULL;
         row = (ROW_ID) radListGetFirst (&resSet->rows))
    {
        radListRemove (&resSet->rows, (NODE_PTR)row);
        raddatabaseRowDescriptionDelete (row);
    }

    free (resSet);
    mysqlId->resSet = NULL;
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
    MYSQL_ID        mysqlId = (MYSQL_ID)id;
    MYSQL_RES       *results;
    char            query[DB_QUERY_LENGTH_MAX];

    sprintf (query, "EXPLAIN %s", tableName);

    if (mysql_query (mysqlId->dbConn, query) != 0)
    {
        return FALSE;
    }

    results = mysql_store_result (mysqlId->dbConn);
    if (results != NULL)
    {
        mysql_free_result (results);
    }

    return TRUE;
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
            sprintf (fType, "INT");
        }
        else if (field->type & FIELD_BIGINT)
        {
            sprintf (fType, "BIGINT");
        }
        else if (field->type & FIELD_FLOAT)
        {
            sprintf (fType, "FLOAT");
        }
        else if (field->type & FIELD_DOUBLE)
        {
            sprintf (fType, "DOUBLE");
        }
        else if (field->type & FIELD_DATETIME)
        {
            sprintf (fType, "DATETIME");
        }
        else
        {
            sprintf (fType, "CHAR(%d)", field->cvalLength);
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
            index += sprintf (&query[index], "UNIQUE INDEX (%s),",
                              field->name);
            continue;
        }
        else if (field->type & FIELD_INDEX)
        {
            index += sprintf (&query[index], "INDEX (%s),",
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
    MYSQL_ID        mysqlId = (MYSQL_ID)id;
    char            query[DB_QUERY_LENGTH_MAX];
    MYSQL_RES       *results;
    MYSQL_ROW  row;
    MYSQL_FIELD  *field;
    UINT         i, j;
    FIELD_ID        genField;
    ULONG           *fieldLengths;

    newRow = (ROW_ID) malloc (sizeof (*newRow));
    if (newRow == NULL)
    {
        radMsgLog(PRI_STATUS, "raddatabaseTableDescriptionGet: malloc failed!");
        return NULL;
    }
    memset (newRow, 0, sizeof (*newRow));
    radListReset (&newRow->fields);

    sprintf (query, "SHOW COLUMNS FROM %s", tableName);

    if (mysql_query (mysqlId->dbConn, query) != 0) /* the query failed */
    {
        radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableDescriptionGet: mysql_query():"
                " %s", printError (mysqlId));
        free (newRow);
        return NULL;
    }

    /* the query succeeded; determine whether or not it returns data */

    results = mysql_store_result (mysqlId->dbConn);
    if (results == NULL) /* no result set was returned */
    {
        /*
         * does the lack of a result set mean that an error
         * occurred or that no result set was returned?
         */
        if (mysql_field_count (mysqlId->dbConn) > 0)
        {
            /*
             * a result set was expected, but mysql_store_result()
             * did not return one; this means an error occurred
             */
            radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableDescriptionGet:"
                    " problem processing result set:"
                    " %s", printError (mysqlId));
        }
        else
        {
            /*
             * no result set was returned; query returned no data
             * (it was not a SELECT, SHOW, DESCRIBE, or EXPLAIN),
             * so just report number of rows affected by query
             */
            radMsgLog(PRI_STATUS, "raddatabaseTableDescriptionGet:"
                    " table is empty!");
        }

        free (newRow);
        return NULL;
    }

    /*  ... a result set was returned
        ... store column descriptions in the generic row structure
    */
    while ((row = mysql_fetch_row (results)) != NULL)
    {
        /*  ... get generic row memory
        */
        genField = (FIELD_ID) malloc (sizeof (*genField));
        if (genField == NULL)
        {
            radMsgLog(PRI_CATASTROPHIC, "raddatabaseTableDescriptionGet: "
                    "malloc failed!");
            mysql_free_result (results);
            freeRow (newRow);
            return NULL;
        }
        memset (genField, 0, sizeof (*genField));

        fieldLengths = mysql_fetch_lengths (results);

        mysql_field_seek (results, 0);
        for (i = 0; i < mysql_num_fields (results); i++)
        {
            field = mysql_fetch_field (results);

            for (j = 0; j < strlen (field->name); j ++)
            {
                field->name[j] = toupper (field->name[j]);
            }

            if (!strcmp (field->name, "FIELD"))
            {
                strncpy (genField->name, row[i], DB_FIELD_NAME_MAX);
            }
            else if (!strcmp (field->name, "TYPE"))
            {
                for (j = 0; j < strlen (row[i]); j ++)
                {
                    row[i][j] = toupper (row[i][j]);
                }
                if (!strcmp (row[i], "TINYINT") || 
                    !strcmp (row[i], "SMALLINT") ||
                    !strcmp (row[i], "MEDIUMINT") ||
                    !strcmp (row[i], "INT"))
                {
                    genField->type = FIELD_INT;
                }
                else if (!strcmp (row[i], "BIGINT"))
                {
                    genField->type = FIELD_BIGINT;
                }
                else if (!strcmp (row[i], "FLOAT"))
                {
                    genField->type = FIELD_FLOAT;
                }
                else if (!strcmp (row[i], "DOUBLE"))
                {
                    genField->type = FIELD_DOUBLE;
                }
                else if (!strcmp (row[i], "DATETIME"))
                {
                    genField->type = FIELD_DATETIME;
                }
                else
                {
                    genField->type = FIELD_STRING;
                }

                /*  ... set to be unused by default
                */
                genField->type |= FIELD_VALUE_IS_NULL;
            }
            else if (!strcmp (field->name, "NULL"))
            {
                if (row[i] == NULL)
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

    mysql_free_result (results);
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
    int             index;

    index = sprintf (query, "INSERT INTO %s SET ", tableName);

    /*  ... build all the "SET" values
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
            index += sprintf (&query[index], "%s = %d,",
                              field->name, field->ivalue);
        }
        else if (field->type & FIELD_BIGINT)
        {
            index += sprintf (&query[index], "%s = %lld,",
                              field->name, field->llvalue);
        }
        else if (field->type & FIELD_FLOAT)
        {
            index += sprintf (&query[index], "%s = %f,",
                              field->name, field->fvalue);
        }
        else if (field->type & FIELD_DOUBLE)
        {
            index += sprintf (&query[index], "%s = %f,",
                              field->name, field->dvalue);
        }
        else
        {
            index += sprintf (&query[index], "%s = \"%s\",",
                              field->name, field->cvalue);
        }
    }

    /*  ... get rid of the trailing ','
    */
    query[index-1] = 0;
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
