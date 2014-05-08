/*-----------------------------------------------------------------------
 
  FILENAME:
        radsqlite.c
 
  PURPOSE:
        Provide the database I/F utilities - sqlite-specific.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Function
        07/22/2008      M.S. Teel       0               Original
 
  ASSUMPTIONS:
        This is linked with libsqlite3 ...
 
  LICENSE:
        Copyright 2001-2008 Mark S. Teel. All rights reserved.

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
#include <radsqlite.h>

/*  ... Local include files
*/

/*  ... global memory declarations
*/

/*  ... global memory referenced
*/

/*  ... static (local) memory declarations
*/

#define SQLITE_RETRY_INTERVAL           25

// ... define the sqlite-specific work area we return as "SQLITE_DATABASE_ID"
typedef struct
{
    sqlite3*                dbConn;
    SQLITE_RESULT_SET_ID    resSet;
    sqlite3_stmt*           statement;
} *SQLITE_ID;


/* ... methods
*/
static char errorString[512];
static char *printError (SQLITE_ID id)
{
    if (id != NULL)
    {
        sprintf (errorString, "error %d (%s)",
                 (int)sqlite3_errcode (id->dbConn), sqlite3_errmsg (id->dbConn));
    }
    else
    {
        errorString[0] = 0;
    }

    return errorString;
}

static void freeRow (SQLITE_ROW_ID id)
{
    SQLITE_FIELD_ID     node;

    for (node = (SQLITE_FIELD_ID) radListGetFirst (&id->fields);
         node != NULL;
         node = (SQLITE_FIELD_ID) radListGetFirst (&id->fields))
    {
        if (node->type & SQLITE_FIELD_STRING)
        {
            free (node->cvalue);
        }
        radListRemove (&id->fields, (NODE_PTR)node);
        if (id->mallocBlock == NULL)
        {
            free(node);
        }
    }

    if (id->mallocBlock != NULL)
    {
        free(id->mallocBlock);
    }
    free(id);
    return;
}

static int processResultRow (SQLITE_ID sqliteId, sqlite3_stmt* statement)
{
    SQLITE_ROW_ID          genRow;
    SQLITE_FIELD_ID        genField, mallocBlock;
    int                    i;

    genRow = (SQLITE_ROW_ID) malloc (sizeof (*genRow));
    if (genRow == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: malloc failed!");
        return ERROR;
    }
    memset (genRow, 0, sizeof (*genRow));
    radListReset (&genRow->fields);

    // Get all memory for fields in one block:
    genRow->mallocBlock = (SQLITE_FIELD_ID)malloc(sqlite3_column_count(statement) * sizeof(SQLITE_FIELD));
    if (genRow->mallocBlock == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: field block malloc failed!");
        freeRow (genRow);
        return ERROR;
    }

    // Loop through all returned columns:
    for (i = 0; i < sqlite3_column_count(statement); i ++)
    {
        genField = &genRow->mallocBlock[i];
        memset (genField, 0, sizeof (*genField));

        strncpy (genField->name, sqlite3_column_name(statement, i), DB_SQLITE_FIELD_NAME_MAX);

        switch (sqlite3_column_type(statement, i))
        {
            case SQLITE_INTEGER:
                // Treat all ints as 64-bit:
                genField->type |= SQLITE_FIELD_BIGINT;
                genField->llvalue = sqlite3_column_int64(statement, i);
                break;
            case SQLITE_FLOAT:
                // Treat all floats as double:
                genField->type |= SQLITE_FIELD_DOUBLE;
                genField->dvalue = sqlite3_column_double(statement, i);
                break;
            case SQLITE_NULL:
                genField->type = SQLITE_FIELD_VALUE_IS_NULL;
                break;
            case SQLITE_TEXT:
                genField->type |= SQLITE_FIELD_STRING;
                genField->cvalue = malloc (sqlite3_column_bytes(statement, i) + 1);
                if (genField->cvalue == NULL)
                {
                    radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: malloc failed!");
                    freeRow (genRow);
                    free (genField);
                    return ERROR;
                }
                memcpy (genField->cvalue, sqlite3_column_text(statement, i), sqlite3_column_bytes(statement, i));
                genField->cvalue[sqlite3_column_bytes(statement, i)] = 0;
                genField->cvalLength = sqlite3_column_bytes(statement, i);
                break;
        }

        // add to the generic field list here...
        radListAddToEnd (&genRow->fields, (NODE_PTR)genField);
    }

    // add to the generic result row list here ...
    radListAddToEnd (&sqliteId->resSet->rows, (NODE_PTR)genRow);

    return OK;
}


/************************** Database - Level ******************************
 **************************************************************************
*/
/*  ... connect to a database server;
    ... 'hostORfile' should be the full path of the db filename;
    ... 'username' and 'password' and 'dbName' are ignored;
    ... returns the ID or NULL
*/
SQLITE_DATABASE_ID radsqliteOpen (const char *dbFile)
{
    SQLITE_ID   newId;

    newId = (SQLITE_ID) malloc(sizeof (*newId));
    if (newId == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteOpen: malloc failed!");
        return NULL;
    }

    memset(newId, 0, sizeof (*newId));


    //  ...allocate, initialize connection handler:
    if (sqlite3_open(dbFile, &newId->dbConn) != SQLITE_OK)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteOpen: sqlite3_open() failed: %s",
                  printError (newId));

        if (newId->dbConn != NULL)
        {
            sqlite3_close(newId->dbConn);
            newId->dbConn = NULL;
        }

        free(newId);
        return NULL;
    }

    return (SQLITE_DATABASE_ID)newId;
}


/*  ... close a database server connection
*/
void radsqliteClose (SQLITE_DATABASE_ID id)
{
    SQLITE_ID   sqliteId = (SQLITE_ID)id;

    if (sqlite3_close(sqliteId->dbConn) != SQLITE_OK)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteClose: %s", printError(sqliteId));
    }

    if (sqliteId->resSet)
    {
        radsqliteReleaseResults(id, sqliteId->resSet);
    }

    free(sqliteId);
    return;
}

/*  ... issue an SQL query to the db engine;
    ... 'createResults' should be set to TRUE if a result set should
    ... be created for retrieval with the radsqliteTableGetResults function
    ... described below, otherwise set 'createResults' to FALSE;
    ... if results are generated it can be VERY slow and eat up a lot of 
    ... heap space; the good news is that once the SQLITE_RESULT_SET_ID is 
    ... retrieved, it can persist as long as the user needs to keep it 
    ... without adverse effects on the db server;
    ... returns OK or ERROR if there is a db server error, query error,
    ... or 'createResults' is set to TRUE and no result set is generated 
    ... by the 'query'
*/
int radsqliteQuery
(
    SQLITE_DATABASE_ID  id,
    const char          *query,
    int                 createResults
)
{
    SQLITE_ID       sqliteId = (SQLITE_ID)id;
    sqlite3_stmt    *statement;
    const char      *tail;
    int             tries, queryResult, done = FALSE;

    if (strlen(query) > DB_SQLITE_QUERY_LENGTH_MAX-1)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: "
                   "query string longer than %d characters...",
                   DB_SQLITE_QUERY_LENGTH_MAX-1);
        return ERROR;
    }

    sqliteId->resSet = NULL;
    if (createResults)
    {
        // Create the result set:
        sqliteId->resSet = (SQLITE_RESULT_SET_ID) malloc(sizeof (*(sqliteId->resSet)));
        if (sqliteId->resSet == NULL)
        {
            radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: malloc failed!");
            return ERROR;
        }
    
        memset(sqliteId->resSet, 0, sizeof (*(sqliteId->resSet)));
        radListReset(&sqliteId->resSet->rows);
        strncpy(sqliteId->resSet->query, query, DB_SQLITE_QUERY_LENGTH_MAX-1);
    }

// radMsgLog(PRI_STATUS, "radsqliteQuery: submitting query: %s", query);

    for (tries = 0; tries < SQLITE_MAX_QUERY_TRIES; tries ++)
    {
        queryResult = sqlite3_prepare(sqliteId->dbConn,
                                      query,
                                      strlen(query) + 1,
                                      &statement,
                                      &tail);
        if (queryResult == SQLITE_OK)
        {
            // We're done:
            break;
        }
        else if (queryResult == SQLITE_BUSY || queryResult == SQLITE_LOCKED)
        {
            // Try again:
            radMsgLog(PRI_MEDIUM, "radsqliteQuery: database locked, retry...");
            radUtilsSleep(100);
            continue;
        }
        else
        {
            radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: sqlite3_prepare():"
                       " %s", printError (sqliteId));
            radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: query failed: %s", query);
            sqliteId->resSet = NULL;
            return ERROR;
        }
    }

    // Did we have a good result above:
    if (queryResult != SQLITE_OK)
    {
        // Nope:
        radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: query failed: %s", query);
        radsqliteReleaseResults(id, sqliteId->resSet);
        sqliteId->resSet = NULL;
        return ERROR;
    }

    // Begin execution of the query:
    while (! done)
    {
        queryResult = sqlite3_step(statement);
        switch (queryResult)
        {
            case SQLITE_ROW:
                if (createResults == TRUE)
                {
                    if (processResultRow(sqliteId, statement) == ERROR)
                    {
                        radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery:"
                                   " processResultRow failed: %s",
                                   printError (sqliteId));
                        radsqliteReleaseResults(id, sqliteId->resSet);
                        sqliteId->resSet = NULL;
                        sqlite3_finalize(statement);
                        return ERROR;
                    }
                }
                break;
            case SQLITE_DONE:
                done = TRUE;
                break;
            case SQLITE_BUSY:
            case SQLITE_LOCKED:
                // keep trying:
                radUtilsSleep(SQLITE_RETRY_INTERVAL);
                break;
            case SQLITE_ERROR:
            case SQLITE_MISUSE:
            default:
                radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery:"
                           " sqlite3_step failed: %d: %s",
                           queryResult, printError (sqliteId));
                if (createResults)
                {
                    radsqliteReleaseResults(id, sqliteId->resSet);
                    sqliteId->resSet = NULL;
                }
                sqlite3_finalize(statement);
                return ERROR;
        }
    }

    if (sqlite3_finalize(statement) != SQLITE_OK)
    {
        return ERROR;
    }

    if (createResults && (radListGetNumberOfNodes(&sqliteId->resSet->rows) == 0))
    {
        return ERROR;
    }

    return OK;
}


/*  ... retrieve the result set if there is one;
    ... should be called immediately after radsqliteQuery if the
    ... query was supposed to generate a result set (SELECT, SHOW, etc.);
    ... returns NULL if there is no result set available;
    ... SQLITE_RESULT_SET_ID should be released via radsqliteTableResultsRelease
    ... once the user is finished with it;
    ... returns SQLITE_RESULT_SET_ID or NULL
*/
SQLITE_RESULT_SET_ID radsqliteGetResults
(
    SQLITE_DATABASE_ID  id
)
{
    SQLITE_ID           sqliteId = (SQLITE_ID)id;

    return sqliteId->resSet;
}


/*  ... refresh and replace a result set based on the original query;
    ... can be even SLOWER;
    ... SQLITE_RESULT_SET_ID should be released via radsqliteTableResultsRelease
    ... once the user is finished with it;
    ... returns a new SQLITE_RESULT_SET_ID or NULL
*/
SQLITE_RESULT_SET_ID radsqliteRefreshResults
(
    SQLITE_DATABASE_ID      id,
    SQLITE_RESULT_SET_ID    resSetId
)
{
    char                    query[DB_SQLITE_QUERY_LENGTH_MAX];
    SQLITE_RESULT_SET_ID    newSetId;

    strncpy(query, resSetId->query, DB_SQLITE_QUERY_LENGTH_MAX-1);

    radsqliteReleaseResults(id, resSetId);

    if (radsqliteQuery(id, query, TRUE) == ERROR)
    {
        return NULL;
    }

    if ((newSetId = radsqliteGetResults(id)) == NULL)
    {
        return NULL;
    }

    return newSetId;
}


/*  ... release a result set
*/
void radsqliteReleaseResults
(
    SQLITE_DATABASE_ID      id,
    SQLITE_RESULT_SET_ID    resSet
)
{
    SQLITE_ID               sqliteId = (SQLITE_ID)id;
    SQLITE_ROW_ID           row;

    for (row = (SQLITE_ROW_ID) radListGetFirst(&resSet->rows);
         row != NULL;
         row = (SQLITE_ROW_ID) radListGetFirst(&resSet->rows))
    {
        radListRemove(&resSet->rows, (NODE_PTR)row);
        radsqliteRowDescriptionDelete(row);
    }

    free(resSet);
    sqliteId->resSet = NULL;
    return;
}



/*************************** Table - Level ********************************
 **************************************************************************
*/

/*  ... does 'tableName' table exist?;
    ... returns TRUE or FALSE
*/
int radsqliteTableIfExists
(
    SQLITE_DATABASE_ID      id,
    const char              *tableName
)
{
    SQLITE_ID               sqliteId = (SQLITE_ID)id;
    SQLITE_RESULT_SET_ID    results;
    char                    query[DB_SQLITE_QUERY_LENGTH_MAX];
    int                     retVal = FALSE;

    sprintf (query, "SELECT * FROM sqlite_master where tbl_name='%s'", tableName);

    if (radsqliteQuery(sqliteId, query, TRUE) != OK)
    {
        return FALSE;
    }

    results = radsqliteGetResults(sqliteId);
    if (results != NULL)
    {
        if (radListGetNumberOfNodes(&results->rows) > 0)
        {
            retVal = TRUE;
        }
        radsqliteReleaseResults(sqliteId, results);
    }

    return retVal;
}


/*  ... table management; all return OK or ERROR
*/
int radsqliteTableCreate
(
    SQLITE_DATABASE_ID  id,
    const char          *tableName,
    SQLITE_ROW_ID       rowDescr        /* see radsqliteRowDescriptionCreate */
)
{
    char                query[DB_SQLITE_QUERY_LENGTH_MAX];
    char                fType[12], notNull[12];
    int                 index = 0, priKey = FALSE;
    SQLITE_FIELD_ID     field;

    index = sprintf(query, "CREATE TABLE %s ( ", tableName);

    /*  ... do the column definitions first
    */
    for (field = (SQLITE_FIELD_ID) radListGetFirst(&rowDescr->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext(&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->name[0] == 0)
        {
            radMsgLog(PRI_MEDIUM, "radsqliteTableCreate: field name is empty!");
            return ERROR;
        }
        if (field->type == 0)
        {
            radMsgLog(PRI_MEDIUM, "radsqliteTableCreate: type is empty!");
            return ERROR;
        }
        if ((field->type & SQLITE_FIELD_STRING) && (field->cvalLength == 0))
        {
            radMsgLog(PRI_MEDIUM, "radsqliteTableCreate: cval length is 0!");
            return ERROR;
        }

        if (field->type & SQLITE_FIELD_BIGINT)
        {
            sprintf (fType, "INTEGER");
        }
        else if (field->type & SQLITE_FIELD_DOUBLE)
        {
            sprintf (fType, "REAL");
        }
        else
        {
            sprintf (fType, "TEXT");
        }

        if (field->type & SQLITE_FIELD_NOT_NULL)
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
    for (field = (SQLITE_FIELD_ID) radListGetFirst(&rowDescr->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext(&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->type & SQLITE_FIELD_PRI_KEY)
        {
            if (priKey)
            {
                radMsgLog(PRI_MEDIUM, "radsqliteTableCreate: "
                        "more than one PRIMARY KEY specified!");
                return ERROR;
            }

            index += sprintf(&query[index], "PRIMARY KEY (%s),",
                             field->name);
            priKey = TRUE;
            continue;
        }
        else if (field->type & SQLITE_FIELD_UNIQUE_INDEX)
        {
            index += sprintf(&query[index], "UNIQUE INDEX (%s),",
                             field->name);
            continue;
        }
        else if (field->type & SQLITE_FIELD_INDEX)
        {
            index += sprintf(&query[index], "INDEX (%s),",
                             field->name);
            continue;
        }
    }

    if (query[index-1] == ',')
    {
        index --;
    }

    sprintf (&query[index], " )");

    return (radsqliteQuery(id, query, FALSE));
}

int radsqliteTableDelete
(
    SQLITE_DATABASE_ID  id,
    const char          *tableName
)
{
    char                query[DB_SQLITE_QUERY_LENGTH_MAX];

    sprintf(query, "DROP TABLE %s", tableName);

    return (radsqliteQuery(id, query, FALSE));
}

int radsqliteTableTruncate
(
    SQLITE_DATABASE_ID  id,
    const char          *tableName
)
{
    char                query[DB_SQLITE_QUERY_LENGTH_MAX];

    sprintf(query, "DELETE FROM %s", tableName);

    return (radsqliteQuery(id, query, FALSE));
}

/*  ... create a row description to use for row insertion/retrieval;
    ... must be deleted with radsqliteRowDescriptionDelete after use;
    ... sets the SQLITE_FIELD_VALUE_IS_NULL bit in field->type for all fields -
    ... this means the user must clear this bit for field values to be 
    ... used in queries/insertions/deletions;
    ... returns SQLITE_ROW_ID or NULL
*/
SQLITE_ROW_ID radsqliteTableDescriptionGet
(
    SQLITE_DATABASE_ID      id,
    const char              *tableName
)
{
    SQLITE_ROW_ID           newRow, row;
    SQLITE_ID               sqliteId = (SQLITE_ID)id;
    char                    query[DB_SQLITE_QUERY_LENGTH_MAX];
    SQLITE_RESULT_SET_ID    results;
    UINT                    j, numRows;
    SQLITE_FIELD_ID         genField, tempField, mallocBlock;

    newRow = (SQLITE_ROW_ID) malloc(sizeof (*newRow));
    if (newRow == NULL)
    {
        radMsgLog(PRI_STATUS, "radsqliteTableDescriptionGet: malloc failed!");
        return NULL;
    }
    memset(newRow, 0, sizeof (*newRow));
    radListReset(&newRow->fields);

    sprintf(query, "PRAGMA TABLE_INFO(%s)", tableName);

    if (radsqliteQuery(sqliteId, query, TRUE) != OK)   /* the query failed */
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteTableDescriptionGet: "
                   "%s", printError (sqliteId));
        free(newRow);
        return NULL;
    }

    /* the query succeeded; determine whether or not it returns data */
    results = radsqliteGetResults(sqliteId);
    if (results != NULL)
    {
        if (radListGetNumberOfNodes(&results->rows) <= 0)
        {
            radsqliteReleaseResults(sqliteId, results);
            free (newRow);
            return NULL;
        }
    }
    else
    {
        free (newRow);
        return NULL;
    }

    /*  ... a result set was returned
        ... store column descriptions in the generic row structure
    */
    numRows = radsqliteResultsGetRowCount(results);
    newRow->mallocBlock = (SQLITE_FIELD_ID)malloc(numRows * sizeof(SQLITE_FIELD));
    if (newRow->mallocBlock == NULL)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqliteTableDescriptionGet: "
                   "field malloc failed!");
        radsqliteReleaseResults (sqliteId, results);
        freeRow (newRow);
        return NULL;
    }

    for (genField = newRow->mallocBlock, row = radsqliteResultsGetFirst(results);
         row != NULL;
         genField ++, row = radsqliteResultsGetNext(results))
    {
        memset (genField, 0, sizeof (*genField));

        // Get "name":
        tempField = radsqliteFieldGet(row, "name");
        if (tempField == NULL)
        {
            radMsgLog(PRI_CATASTROPHIC, "radsqliteTableDescriptionGet: "
                       "field 1 failed!");
            free (newRow->mallocBlock);
            radsqliteReleaseResults(sqliteId, results);
            freeRow (newRow);
            return NULL;
        }

        strncpy (genField->name, tempField->cvalue, DB_SQLITE_FIELD_NAME_MAX);

        // Get "type":
        tempField = radsqliteFieldGet(row, "type");
        if (tempField == NULL)
        {
            radMsgLog(PRI_CATASTROPHIC, "radsqliteTableDescriptionGet: "
                       "field 2 failed!");
            free (newRow->mallocBlock);
            radsqliteReleaseResults(sqliteId, results);
            freeRow (newRow);
            return NULL;
        }

        for (j = 0; j < strlen (tempField->cvalue); j ++)
        {
            tempField->cvalue[j] = toupper(tempField->cvalue[j]);
        }
        if (!strcmp(tempField->cvalue, "INTEGER"))
        {
            genField->type = SQLITE_FIELD_BIGINT;
        }
        else if (!strcmp (tempField->cvalue, "REAL"))
        {
            genField->type = SQLITE_FIELD_DOUBLE;
        }
        else
        {
            genField->type = SQLITE_FIELD_STRING;
        }

        /*  ... set to be unused by default
        */
        genField->type |= SQLITE_FIELD_VALUE_IS_NULL;

        /*  ... add to the generic field list here...
        */
        radListAddToEnd(&newRow->fields, (NODE_PTR)genField);
    }

    radsqliteReleaseResults(sqliteId, results);
    return newRow;
}

/*  ... query a table to create a result set given a row description;
    ... columns to be included in the result set should have SQLITE_FIELD_DISPLAY
    ... set in the appropriate SQLITE_FIELD_ID.type within 'rowDescription';
    ... columns NOT to be included in the WHERE clause should have
    ... SQLITE_FIELD_VALUE_IS_NULL set in SQLITE_FIELD_ID.type;
    ... can be VERY slow and eat up a lot of heap space; the good news 
    ... is that once the SQLITE_RESULT_SET_ID is retrieved, it can persist as 
    ... long as the user needs to keep it without adverse effects on 
    ... the db server;
    ... SQLITE_RESULT_SET_ID should be released via radsqliteTableResultsRelease
    ... once the user is finished with it;
    ... only the populated fields in 'rowDescription' will be used to 
    ... match records in the table (i.e., multiple rows can be matched)
    ... returns SQLITE_RESULT_SET_ID or NULL
*/
SQLITE_RESULT_SET_ID radsqliteTableQueryRow
(
    SQLITE_DATABASE_ID  id,
    const char          *tableName,
    SQLITE_ROW_ID       rowDescr
)
{
    char                query[DB_SQLITE_QUERY_LENGTH_MAX];
    char                select[DB_SQLITE_QUERY_LENGTH_MAX];
    char                where[DB_SQLITE_QUERY_LENGTH_MAX];
    int                 index, firstWhere = TRUE;
    SQLITE_FIELD_ID     field;

    /*  ... build the select clause
    */
    index = 0;
    for (field = (SQLITE_FIELD_ID) radListGetFirst (&rowDescr->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext (&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->type & SQLITE_FIELD_DISPLAY)
        {
            index += sprintf (&select[index], "%s,", field->name);
        }
    }
    select[index-1] = 0;

    /*  ... build the where clause
    */
    index = 0;
    for (field = (SQLITE_FIELD_ID) radListGetFirst (&rowDescr->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext (&rowDescr->fields, (NODE_PTR)field))
    {
        if (field->type & SQLITE_FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (!firstWhere)
        {
            index += sprintf (&where[index], "AND ");
        }
        firstWhere = FALSE;

        if (field->type & SQLITE_FIELD_BIGINT)
        {
            index += sprintf (&where[index], "(%s = %lld)",
                              field->name, field->llvalue);
        }
        else if (field->type & SQLITE_FIELD_DOUBLE)
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
    if (radsqliteQuery(id, query, TRUE) == ERROR)
    {
        return NULL;
    }

    return (radsqliteGetResults(id));
}


/*  ... insert a row into a table;
    ... 'rowId' was created with radsqliteTableDescriptionGet then
    ... field values were populated with radsqliteFieldGet, 
    ... radsqliteFieldSetIntValue, etc. prior to this call;
    ... if a 'NOT NULL' field has a NULL value in rowId, it's an error;
    ... returns OK or ERROR
*/
int radsqliteTableInsertRow
(
    SQLITE_DATABASE_ID  id,
    const char          *tableName,
    SQLITE_ROW_ID       rowId
)
{
    SQLITE_FIELD_ID     field;
    char                query[DB_SQLITE_QUERY_LENGTH_MAX];
    char                columns[DB_SQLITE_QUERY_LENGTH_MAX];
    char                values[DB_SQLITE_QUERY_LENGTH_MAX];
    int                 colindex, valindex;

    sprintf (query, "INSERT INTO %s ", tableName);
    colindex = sprintf (columns, "(");
    valindex = sprintf (values, " VALUES (");
    

    /*  ... build the columns and values strings
    */
    for (field = (SQLITE_FIELD_ID) radListGetFirst(&rowId->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext(&rowId->fields, (NODE_PTR)field))
    {
        // Is this a NOT NULL field with NULL value?
        if ((field->type & SQLITE_FIELD_NOT_NULL) && (field->type & SQLITE_FIELD_VALUE_IS_NULL))
        {
            radMsgLog(PRI_MEDIUM, "radsqliteTableInsertRow: "
                       "NOT NULL field has NULL value!");
            return ERROR;
        }

        if (field->type & SQLITE_FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (field->type & SQLITE_FIELD_BIGINT)
        {
            colindex += sprintf (&columns[colindex], "%s,",
                                 field->name);
            valindex += sprintf (&values[valindex], "%lld,",
                                 field->llvalue);
        }
        else if (field->type & SQLITE_FIELD_DOUBLE)
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

    // get rid of the trailing ','s:
    columns[colindex-1] = ')';
    values[valindex-1] = ')';
    
    strcat (query, columns);
    strcat (query, values);

    return (radsqliteQuery(id, query, FALSE));
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
int radsqliteTableModifyRows
(
    SQLITE_DATABASE_ID  id,
    const char          *tableName,
    SQLITE_ROW_ID       matchId,
    SQLITE_ROW_ID       newData
)
{
    char                query[DB_SQLITE_QUERY_LENGTH_MAX];
    char                set[DB_SQLITE_QUERY_LENGTH_MAX];
    char                where[DB_SQLITE_QUERY_LENGTH_MAX];
    int                 index, firstWhere = TRUE;
    SQLITE_FIELD_ID     field;

    /*  ... build the set clause
    */
    index = 0;
    for (field = (SQLITE_FIELD_ID) radListGetFirst(&newData->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext(&newData->fields, (NODE_PTR)field))
    {
        /*      ... is this a NOT NULL field with NULL value?
        */
        if ((field->type & SQLITE_FIELD_NOT_NULL) &&
                (field->type & SQLITE_FIELD_VALUE_IS_NULL))
        {
            radMsgLog(PRI_MEDIUM, "radsqliteTableModifyRows: "
                    "NOT NULL field has NULL value!");
            return ERROR;
        }
        if (field->type & SQLITE_FIELD_VALUE_IS_NULL)
        {
            index += sprintf (&set[index], "%s = NULL,",
                              field->name);
        }

        if (field->type & SQLITE_FIELD_BIGINT)
        {
            index += sprintf (&set[index], "%s = %lld,",
                              field->name, field->llvalue);
        }
        else if (field->type & SQLITE_FIELD_DOUBLE)
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
    for (field = (SQLITE_FIELD_ID) radListGetFirst(&matchId->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext(&matchId->fields, (NODE_PTR)field))
    {
        if (field->type & SQLITE_FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (!firstWhere)
        {
            index += sprintf (&where[index], "AND ");
        }
        firstWhere = FALSE;

        if (field->type & SQLITE_FIELD_BIGINT)
        {
            index += sprintf (&where[index], "(%s = %lld)",
                              field->name, field->llvalue);
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
    return (radsqliteQuery (id, query, FALSE));
}

/*  ... delete rows in a table matching 'matchId';
    ... only the non-NULL fields will be used to match records
    ... in the table (i.e., multiple rows can be matched)
    ... returns OK or ERROR
*/
int radsqliteTableDeleteRows
(
    SQLITE_DATABASE_ID  id,
    const char          *tableName,
    SQLITE_ROW_ID       matchId
)
{
    SQLITE_FIELD_ID     field;
    char                query[DB_SQLITE_QUERY_LENGTH_MAX];
    int                 index, firstWhere = TRUE;

    index = sprintf (query, "DELETE FROM %s WHERE ", tableName);

    /*  ... build all the "WHERE" clause
    */
    for (field = (SQLITE_FIELD_ID) radListGetFirst(&matchId->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext(&matchId->fields, (NODE_PTR)field))
    {
        if (field->type & SQLITE_FIELD_VALUE_IS_NULL)
        {
            continue;
        }

        if (!firstWhere)
        {
            index += sprintf (&query[index], " AND ");
        }
        firstWhere = FALSE;

        if (field->type & SQLITE_FIELD_BIGINT)
        {
            index += sprintf (&query[index], "(%s = %lld)",
                              field->name, field->llvalue);
        }
        else if (field->type & SQLITE_FIELD_DOUBLE)
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

    return (radsqliteQuery (id, query, FALSE));
}

/**************************** Row - Level *********************************
 **************************************************************************
*/

/*  ... traverse the result set row by row
*/
SQLITE_ROW_ID radsqliteResultsGetFirst (SQLITE_RESULT_SET_ID id)
{
    id->current = (SQLITE_ROW_ID)radListGetFirst(&id->rows);
    return id->current;
}

SQLITE_ROW_ID radsqliteResultsGetNext (SQLITE_RESULT_SET_ID id)
{
    id->current = (SQLITE_ROW_ID)radListGetNext(&id->rows, (NODE_PTR)id->current);
    return id->current;
}

SQLITE_ROW_ID radsqliteResultsGetPrev (SQLITE_RESULT_SET_ID id)
{
    id->current = (SQLITE_ROW_ID)radListGetPrevious(&id->rows, (NODE_PTR)id->current);
    return id->current;
}

SQLITE_ROW_ID radsqliteResultsGetLast (SQLITE_RESULT_SET_ID id)
{
    id->current = (SQLITE_ROW_ID)radListGetLast(&id->rows);
    return id->current;
}

int radsqliteResultsGetRowCount (SQLITE_RESULT_SET_ID id)
{
    return radListGetNumberOfNodes(&id->rows);
}

/*  ... create a row description to use when creating a new table;
    ... returns SQLITE_ROW_ID or NULL
*/
SQLITE_ROW_ID radsqliteRowDescriptionCreate (void)
{
    SQLITE_ROW_ID      newId;

    newId = (SQLITE_ROW_ID) malloc(sizeof (*newId));
    if (newId == NULL)
    {
        radMsgLog(PRI_MEDIUM, "radsqliteRowDescriptionCreate: malloc failed!");
        return NULL;
    }

    memset (newId, 0, sizeof (*newId));
    radListReset(&newId->fields);
    return newId;
}


/*  ... returns OK or ERROR
*/
int radsqliteRowDescriptionAddField
(
    SQLITE_ROW_ID       id,
    const char          *name,
    UINT                type,
    int                 maxLength
)
{
    SQLITE_FIELD_ID     field;

    // If this row was allocated as a block, fail here:
    if (id->mallocBlock != NULL)
    {
        radMsgLog(PRI_MEDIUM, "radsqliteRowDescriptionAddField: row was allocated as a block!");
        return ERROR;
    }

    field = (SQLITE_FIELD_ID) malloc (sizeof (*field));
    if (field == NULL)
    {
        radMsgLog(PRI_MEDIUM, "radsqliteRowDescriptionAddField: malloc failed!");
        return ERROR;
    }
    memset (field, 0, sizeof (*field));

    strncpy (field->name, name, DB_SQLITE_FIELD_NAME_MAX-1);
    field->type = type;
    if (field->type & SQLITE_FIELD_STRING)
    {
        field->cvalue = malloc(maxLength);
        if (field->cvalue == NULL)
        {
            free(field);
            return ERROR;
        }
        memset(field->cvalue, 0, maxLength);
    }
    field->cvalLength = maxLength;

    radListAddToEnd(&id->fields, (NODE_PTR)field);
    return OK;
}

int radsqliteRowDescriptionRemoveField
(
    SQLITE_ROW_ID   id,
    const char      *name
)
{
    SQLITE_FIELD_ID field;

    // If this row was allocated as a block, fail here:
    if (id->mallocBlock != NULL)
    {
        radMsgLog(PRI_MEDIUM, "radsqliteRowDescriptionRemoveField: row was allocated as a block!");
        return ERROR;
    }

    field = radsqliteFieldGet(id, name);
    if (field == NULL)
    {
        return ERROR;
    }

    radListRemove(&id->fields, (NODE_PTR)field);
    if (field->type & SQLITE_FIELD_STRING)
    {
        free (field->cvalue);
    }

    free (field);
    return OK;
}

void radsqliteRowDescriptionDelete
(
    SQLITE_ROW_ID       row
)
{
    freeRow(row);
}

/*************************** Field - Level ********************************
 **************************************************************************
*/

/*  ... get the field of interest; returns SQLITE_FIELD_ID or NULL
*/
SQLITE_FIELD_ID radsqliteFieldGet
(
    SQLITE_ROW_ID       id,
    const char          *fieldName
)
{
    SQLITE_FIELD_ID     field;

    for (field = (SQLITE_FIELD_ID) radListGetFirst(&id->fields);
         field != NULL;
         field = (SQLITE_FIELD_ID) radListGetNext(&id->fields, (NODE_PTR)field))
    {
        if (!strcasecmp (field->name, fieldName))
        {
            return field;
        }
    }

    return NULL;
}

/*  ... field extractions; if SQLITE_FIELD_ID is bogus, these will blow chunks!
*/
UINT radsqliteFieldGetType
(
    SQLITE_FIELD_ID id
)
{
    return id->type;
}

long long radsqliteFieldGetBigIntValue
(
    SQLITE_FIELD_ID id
)
{
    return id->llvalue;
}

double radsqliteFieldGetDoubleValue
(
    SQLITE_FIELD_ID id
)
{
    return id->dvalue;
}

char *radsqliteFieldGetCharValue
(
    SQLITE_FIELD_ID id
)
{
    return id->cvalue;
}

int radsqliteFieldGetCharLength
(
    SQLITE_FIELD_ID id
)
{
    return id->cvalLength;
}

/*  ... does not overwrite traits flags
*/
int radsqliteFieldSetTypeBigInt
(
    SQLITE_FIELD_ID id
)
{
    id->type &= SQLITE_FIELD_TYPE_CLEAR;
    id->type |= SQLITE_FIELD_BIGINT;
    return OK;
}

int radsqliteFieldSetTypeDouble
(
    SQLITE_FIELD_ID id
)
{
    id->type &= SQLITE_FIELD_TYPE_CLEAR;
    id->type |= SQLITE_FIELD_DOUBLE;
    return OK;
}

int radsqliteFieldSetTypeChar
(
    SQLITE_FIELD_ID id
)
{
    id->type &= SQLITE_FIELD_TYPE_CLEAR;
    id->type |= SQLITE_FIELD_STRING;
    return OK;
}


int radsqliteFieldSetToDisplay
(
    SQLITE_FIELD_ID id
)
{
    id->type |= SQLITE_FIELD_DISPLAY;

    return OK;
}


int radsqliteFieldSetToNotDisplay
(
    SQLITE_FIELD_ID id
)
{
    id->type &= ~SQLITE_FIELD_DISPLAY;

    return OK;
}


int radsqliteFieldSetToNull
(
    SQLITE_FIELD_ID id
)
{
    id->type |= SQLITE_FIELD_VALUE_IS_NULL;

    return OK;
}


int radsqliteFieldSetToNotNull
(
    SQLITE_FIELD_ID id
)
{
    id->type &= ~SQLITE_FIELD_VALUE_IS_NULL;

    return OK;
}


int radsqliteFieldSetBigIntValue
(
    SQLITE_FIELD_ID id,
    long long       value
)
{
    id->llvalue = value;
    id->type &= ~SQLITE_FIELD_VALUE_IS_NULL;
    radsqliteFieldSetTypeBigInt (id);
    return OK;
}

int radsqliteFieldSetDoubleValue
(
    SQLITE_FIELD_ID id,
    double          value
)
{
    id->dvalue = value;
    id->type &= ~SQLITE_FIELD_VALUE_IS_NULL;
    radsqliteFieldSetTypeDouble (id);
    return OK;
}

/*  ... sets both value AND length
*/
int radsqliteFieldSetCharValue
(
    SQLITE_FIELD_ID id,
    const char      *value,
    int             valueLength
)
{
    char            *temp;

    if (id->cvalLength < valueLength+1)
    {
        temp = (char *) malloc (valueLength+1);
        if (temp == NULL)
        {
            radMsgLog(PRI_MEDIUM, "radsqliteFieldSetCharValue: malloc failed!");
            return ERROR;
        }

        free (id->cvalue);
        id->cvalue = temp;
    }

    strncpy (id->cvalue, value, valueLength+1);
    id->cvalLength = valueLength;
    id->type &= ~SQLITE_FIELD_VALUE_IS_NULL;
    radsqliteFieldSetTypeChar(id);

    return OK;
}


/*  ... issue an SQL query to the db engine;
    ... 'createResults' should be set to TRUE if a result set should
    ... be created for retrieval with the radsqliteGetResults function
    ... described below, otherwise set 'createResults' to FALSE;
    ... returns OK or ERROR if there is a db server error, query error,
    ... or 'createResults' is set to TRUE and no result set is generated 
    ... by the 'query'
*/
int radsqlitedirectQuery
(
    SQLITE_DATABASE_ID  id,
    const char          *query,
    int                 createResults
)
{
    SQLITE_ID       sqliteId = (SQLITE_ID)id;
    sqlite3_stmt    *statement;
    const char      *tail;
    int             tries, queryResult, done = FALSE;

    if (strlen (query) > DB_SQLITE_QUERY_LENGTH_MAX-1)
    {
        radMsgLog(PRI_CATASTROPHIC, "radsqlitedirectQuery: "
                   "query string longer than %d characters...",
                   DB_SQLITE_QUERY_LENGTH_MAX-1);
        return ERROR;
    }

    sqliteId->resSet    = NULL;
    sqliteId->statement = NULL;

// radMsgLog(PRI_STATUS, "radsqliteQuery: submitting query: %s", query);

    for (tries = 0; tries < SQLITE_MAX_QUERY_TRIES; tries ++)
    {
        queryResult = sqlite3_prepare (sqliteId->dbConn,
                                       query,
                                       strlen(query) + 1,
                                       &statement,
                                       &tail);
        if (queryResult == SQLITE_OK)
        {
            // We're done:
            break;
        }
        else if (queryResult == SQLITE_BUSY || queryResult == SQLITE_LOCKED)
        {
            // Try again:
            radMsgLog(PRI_MEDIUM, "radsqlitedirectQuery: database locked, retry...");
            radUtilsSleep(100);
            continue;
        }
        else
        {
            radMsgLog(PRI_CATASTROPHIC, "radsqlitedirectQuery: sqlite3_prepare():"
                       " %s", printError (sqliteId));
            radMsgLog(PRI_CATASTROPHIC, "radsqlitedirectQuery: query failed: %s", query);
            return ERROR;
        }
    }

    // Did we have a good result above:
    if (queryResult != SQLITE_OK)
    {
        // Nope:
        radMsgLog(PRI_CATASTROPHIC, "radsqlitedirectQuery: query failed: %s", query);
        return ERROR;
    }

    sqliteId->statement = statement;
    return OK;
}


/*  ... retrieve the next direct result row (if there is one);
    ... should be called immediately after radsqlitedirectQuery if the
    ... query was supposed to generate a result set (SELECT, SHOW, etc.);
    ... returns NULL if there is no result row available;
    ... SQLITE_DIRECT_ROW should be released via radsqliteTableResultsRelease
    ... once the user is finished with it;
    ... returns SQLITE_RESULT_SET_ID or NULL
*/
SQLITE_DIRECT_ROW radsqlitedirectGetRow
(
    SQLITE_DATABASE_ID  id
)
{
    SQLITE_ID           sqliteId = (SQLITE_ID)id;
    int                 queryResult;

    if (sqliteId->statement == NULL)
    {
        return NULL;
    }

    // Begin execution of the query:
    while (TRUE)
    {
        queryResult = sqlite3_step(sqliteId->statement);
        switch (queryResult)
        {
            case SQLITE_ROW:
                return sqliteId->statement;
            case SQLITE_DONE:
                return NULL;
            case SQLITE_BUSY:
                // keep trying:
                radUtilsSleep (SQLITE_RETRY_INTERVAL);
                break;
            case SQLITE_ERROR:
            case SQLITE_MISUSE:
            default:
                radMsgLog(PRI_CATASTROPHIC, "radsqlitedirectGetRow:"
                           " sqlite3_step failed: %s",
                           printError (sqliteId));
                return NULL;
        }
    }
}


/*  ... release direct results
*/
void radsqlitedirectReleaseResults
(
    SQLITE_DATABASE_ID      id
)
{
    SQLITE_ID               sqliteId = (SQLITE_ID)id;

    if (sqliteId->statement != NULL)
    {
        sqlite3_finalize(sqliteId->statement);
        sqliteId->statement = NULL;
        return;
    }
}


/*  ... get the direct field of interest; returns SQLITE_FIELD_ID or NULL
*/
SQLITE_FIELD_ID radsqlitedirectFieldGet
(
    SQLITE_DIRECT_ROW   statement,
    const char          *fieldName
)
{
    static SQLITE_FIELD saveField;
    int                 i, maxColumn;

    if (statement == NULL)
    {
        return NULL;
    }

    maxColumn = sqlite3_column_count(statement);

    // Loop through all returned columns:
    for (i = 0; i < maxColumn; i ++)
    {
        if (strcasecmp(sqlite3_column_name(statement, i), fieldName))
        {
            // No match, continue:
            continue;
        }

        memset(&saveField, 0, sizeof(saveField));

        // Save the name:
        strncpy(saveField.name, sqlite3_column_name(statement, i), DB_SQLITE_FIELD_NAME_MAX);

        // Save the data:
        switch (sqlite3_column_type(statement, i))
        {
            case SQLITE_INTEGER:
                // Treat all ints as 64-bit:
                saveField.type |= SQLITE_FIELD_BIGINT;
                saveField.llvalue = sqlite3_column_int64(statement, i);
                break;
            case SQLITE_FLOAT:
                // Treat all floats as double:
                saveField.type |= SQLITE_FIELD_DOUBLE;
                saveField.dvalue = sqlite3_column_double(statement, i);
                break;
            case SQLITE_NULL:
                saveField.type = SQLITE_FIELD_VALUE_IS_NULL;
                break;
            case SQLITE_TEXT:
                saveField.type |= SQLITE_FIELD_STRING;
                if (saveField.cvalue != NULL)
                {
                    free(saveField.cvalue);
                }
                saveField.cvalue = malloc (sqlite3_column_bytes(statement, i) + 1);
                if (saveField.cvalue == NULL)
                {
                    radMsgLog(PRI_CATASTROPHIC, "radsqliteQuery: malloc failed!");
                    return NULL;
                }
                memcpy (saveField.cvalue, sqlite3_column_text(statement, i), sqlite3_column_bytes(statement, i));
                saveField.cvalue[sqlite3_column_bytes(statement, i)] = 0;
                saveField.cvalLength = sqlite3_column_bytes(statement, i);
                break;
        }

        // If here, we found him:
        return &saveField;
    }

    // If here, we didn't find him:
    return NULL;
}

int radsqlitePragma (SQLITE_DATABASE_ID id, const char* pragmaName, const char* value)
{
    char        temp[128];

    sprintf(temp, "PRAGMA %s = %s", pragmaName, value);
    if (radsqlitedirectQuery(id, temp, FALSE) == ERROR)
    {
        return ERROR;
    }

    // Cleanup:
    radsqlitedirectReleaseResults(id);

    return OK;
}

