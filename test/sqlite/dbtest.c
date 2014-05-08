/*---------------------------------------------------------------------
 
 FILE NAME:
        dbtest.c
 
 PURPOSE:
        Main entry point for the database tester.
 
 REVISION HISTORY:
    Date        Programmer  Revision    Function
    01/10/02    M.S. Teel   0           Original
 
 ASSUMPTIONS:
 None.
 
------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <radsysdefs.h>
#include <radmsgLog.h>
#include <radsqlite.h>


/*  ... global references
*/

/*  ... local memory
*/
#define TEST_DB_USER            "testdbuser"
#define TEST_DB_PASSWORD        "testdbpw"
#define TEST_DATABASE           "/home/mteel/dbtest.sdb"
#define TEST_TABLE              "testtable"

/*  ... methods
*/


/*  ... THE entry point
*/
int main (int argc, char *argv[])
{
    DATABASE_ID     dbId;
    char            query[1024];
    ROW_ID          row;
    FIELD_ID        field;
    int             i, retVal;
    char            temp[1024];
    RESULT_SET_ID   resultId;

    radMsgLogInit ("dbtest", TRUE, TRUE);

    if ((dbId = radsqliteOpen (TEST_DATABASE)) == NULL)
    {
        printf ("dbtest: databaseOpen failed\n");
        exit (1);
    }

    if (!radsqliteTableIfExists (dbId, TEST_TABLE))
    {
        printf ("dbtest: table %s does not exist, creating ...\n", TEST_TABLE);
        row = radsqliteRowDescriptionCreate ();
        if (row == NULL)
        {
            printf ("dbtest: databaseRowDescriptionCreate failed!\n");
            radsqliteClose (dbId);
            exit (1);
        }

        retVal = radsqliteRowDescriptionAddField (row, 
                                                  "f1", 
                                                  FIELD_BIGINT | FIELD_PRI_KEY, 
                                                  0);
        if (retVal == ERROR)
        {
            printf ("dbtest: databaseRowDescriptionAddField failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }

        retVal = radsqliteRowDescriptionAddField (row, "f2", FIELD_STRING, 128);
        if (retVal == ERROR)
        {
            printf ("dbtest: databaseRowDescriptionAddField failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }

        // now try to create the table:
        retVal = radsqliteTableCreate (dbId, TEST_TABLE, row);
        if (retVal == ERROR)
        {
            printf ("dbtest: radsqliteTableCreate failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }

        radsqliteRowDescriptionDelete (row);
    }
    else
    {
        if (radsqliteTableTruncate (dbId, TEST_TABLE) != OK)
        {
            printf ("dbtest: radsqliteTableTruncate failed!\n");
            radsqliteClose (dbId);
            exit (1);
        }
    }

    printf ("dbtest: obtaining table description ...\n");
    row = radsqliteTableDescriptionGet (dbId, TEST_TABLE);
    if (row == NULL)
    {
        printf ("dbtest: databaseTableDescriptionGet failed!\n");
        radsqliteClose (dbId);
        exit (1);
    }

    for (field = (FIELD_ID)radListGetFirst(&row->fields);
         field != NULL;
         field = (FIELD_ID)radListGetNext(&row->fields, (NODE_PTR)field))
    {
        printf ("FIELD: %s: type %8.8X\n", field->name, field->type);
    }
    printf ("\n");

    printf ("dbtest: inserting 100 rows ...\n");
    for (i = 0; i < 100; i ++)
    {
        field = radsqliteFieldGet (row, "f1");
        if (field == NULL)
        {
            printf ("dbtest: databaseFieldGet 'f1' failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }
        radsqliteFieldSetBigIntValue (field, (ULONGLONG)i);

        field = radsqliteFieldGet (row, "f2");
        if (field == NULL)
        {
            printf ("dbtest: databaseFieldGet 'f2' failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }
        sprintf (temp, "Test string #%d", i);
        radsqliteFieldSetCharValue (field, temp, strlen (temp));

        /*  ... insert the row
        */
        if (radsqliteTableInsertRow (dbId, TEST_TABLE, row) == ERROR)
        {
            printf ("dbtest: radsqliteTableInsertRow failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }
    }

    printf ("dbtest: deleting odd numbered rows ...\n");
    
    // get a fresh description
    radsqliteRowDescriptionDelete (row);
    row = radsqliteTableDescriptionGet (dbId, TEST_TABLE);
    if (row == NULL)
    {
        printf ("dbtest: databaseTableDescriptionGet failed!\n");
        radsqliteClose (dbId);
        exit (1);
    }    
    
    for (i = 1; i < 100; i += 2)
    {
        field = radsqliteFieldGet (row, "f1");
        if (field == NULL)
        {
            printf ("dbtest: databaseFieldGet 'f1' failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }
        radsqliteFieldSetBigIntValue (field, (ULONGLONG)i);

        /*  ... delete the row
        */
        if (radsqliteTableDeleteRows (dbId, TEST_TABLE, row) == ERROR)
        {
            printf ("dbtest: radsqliteTableDeleteRows failed!\n");
            radsqliteRowDescriptionDelete (row);
            radsqliteClose (dbId);
            exit (1);
        }
    }
    radsqliteRowDescriptionDelete (row);
    
    
    printf ("dbtest: querying for rows between 10 and 20 inclusive ...\n");
    sprintf (query, 
             "SELECT * FROM %s WHERE f1 >= 10 AND f1 <= 20", 
             TEST_TABLE);
    if (radsqliteQuery (dbId, query, TRUE) == ERROR)
    {
        printf ("dbtest: radsqliteQuery failed!\n");
        radsqliteClose (dbId);
        exit (1);
    }

    resultId = radsqliteGetResults (dbId);
    if (resultId == NULL)
    {
        printf ("dbtest: radsqliteGetResults failed!\n");
        radsqliteClose (dbId);
        exit (1);
    }
    
    for (row = radsqliteResultsGetFirst (resultId);
         row != NULL;
         row = radsqliteResultsGetNext (resultId))
    {
        field = radsqliteFieldGet (row, "f1");
        if (field == NULL)
        {
            printf ("dbtest: radsqliteFieldGet failed!\n");
            radsqliteReleaseResults (dbId, resultId);
            radsqliteClose (dbId);
            exit (1);
        }
        printf ("dbtest: f1=%lld : ", radsqliteFieldGetBigIntValue (field));
        
        field = radsqliteFieldGet (row, "f2");
        if (field == NULL)
        {
            printf ("dbtest: radsqliteFieldGet failed!\n");
            radsqliteReleaseResults (dbId, resultId);
            radsqliteClose (dbId);
            exit (1);
        }
        printf ("f2=%s\n", radsqliteFieldGetCharValue (field));
    }
    radsqliteReleaseResults (dbId, resultId);
    
    printf ("dbtest: done!\n");
    
    radsqliteClose (dbId);
    radMsgLogExit ();
    exit (0);
}

