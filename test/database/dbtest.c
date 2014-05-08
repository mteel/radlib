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
#include <raddatabase.h>


/*  ... global references
*/

/*  ... local memory
*/
#define TEST_DB_USER            "testdbuser"
#define TEST_DB_PASSWORD        "testdbpw"
#define TEST_DATABASE           "dbTestDB"
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

    if ((dbId = raddatabaseOpen (NULL,
                                 TEST_DB_USER,
                                 TEST_DB_PASSWORD,
                                 TEST_DATABASE))
        == NULL)
    {
        printf ("dbtest: databaseOpen failed!\n");
        exit (1);
    }

    if (!raddatabaseTableIfExists (dbId, TEST_TABLE))
    {
        printf ("dbtest: table %s does not exist, creating ...\n", TEST_TABLE);
        row = raddatabaseRowDescriptionCreate ();
        if (row == NULL)
        {
            printf ("dbtest: databaseRowDescriptionCreate failed!\n");
            raddatabaseClose (dbId);
            exit (1);
        }

        retVal = raddatabaseRowDescriptionAddField (row, 
                                                    "f1", 
                                                    FIELD_INT | FIELD_UNIQUE_INDEX | FIELD_INDEX | FIELD_PRI_KEY, 
                                                    0);
        if (retVal == ERROR)
        {
            printf ("dbtest: databaseRowDescriptionAddField failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }

        retVal = raddatabaseRowDescriptionAddField (row, "f2", FIELD_STRING, 128);
        if (retVal == ERROR)
        {
            printf ("dbtest: databaseRowDescriptionAddField failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }

        /*      ... now try to create the table
        */
        retVal = raddatabaseTableCreate (dbId, TEST_TABLE, row);
        if (retVal == ERROR)
        {
            printf ("dbtest: raddatabaseTableCreate failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }

        raddatabaseRowDescriptionDelete (row);
    }

    printf ("dbtest: obtaining table description ...\n");
    row = raddatabaseTableDescriptionGet (dbId, TEST_TABLE);
    if (row == NULL)
    {
        printf ("dbtest: databaseTableDescriptionGet failed!\n");
        raddatabaseClose (dbId);
        exit (1);
    }

    printf ("dbtest: inserting 100 rows ...\n");
    for (i = 0; i < 100; i ++)
    {
        field = raddatabaseFieldGet (row, "f1");
        if (field == NULL)
        {
            printf ("dbtest: databaseFieldGet 'f1' failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }
        raddatabaseFieldSetIntValue (field, i);

        field = raddatabaseFieldGet (row, "f2");
        if (field == NULL)
        {
            printf ("dbtest: databaseFieldGet 'f2' failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }
        sprintf (temp, "Test string #%d", i);
        raddatabaseFieldSetCharValue (field, temp, strlen (temp));

        /*  ... insert the row
        */
        if (raddatabaseTableInsertRow (dbId, TEST_TABLE, row) == ERROR)
        {
            printf ("dbtest: raddatabaseTableInsertRow failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }
    }

    printf ("dbtest: deleting odd numbered rows ...\n");
    
    // get a fresh description
    raddatabaseRowDescriptionDelete (row);
    row = raddatabaseTableDescriptionGet (dbId, TEST_TABLE);
    if (row == NULL)
    {
        printf ("dbtest: databaseTableDescriptionGet failed!\n");
        raddatabaseClose (dbId);
        exit (1);
    }    
    
    for (i = 1; i < 100; i += 2)
    {
        field = raddatabaseFieldGet (row, "f1");
        if (field == NULL)
        {
            printf ("dbtest: databaseFieldGet 'f1' failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }
        raddatabaseFieldSetIntValue (field, i);

        /*  ... delete the row
        */
        if (raddatabaseTableDeleteRows (dbId, TEST_TABLE, row) == ERROR)
        {
            printf ("dbtest: raddatabaseTableDeleteRows failed!\n");
            raddatabaseRowDescriptionDelete (row);
            raddatabaseClose (dbId);
            exit (1);
        }
    }
    raddatabaseRowDescriptionDelete (row);
    
    
    printf ("dbtest: querying for rows between 10 and 20 inclusive ...\n");
    sprintf (query, 
             "SELECT * FROM %s WHERE f1 >= 10 AND f1 <= 20", 
             TEST_TABLE);
    if (raddatabaseQuery (dbId, query, TRUE) == ERROR)
    {
        printf ("dbtest: raddatabaseQuery failed!\n");
        raddatabaseClose (dbId);
        exit (1);
    }

    resultId = raddatabaseGetResults (dbId);
    if (resultId == NULL)
    {
        printf ("dbtest: raddatabaseGetResults failed!\n");
        raddatabaseClose (dbId);
        exit (1);
    }
    
    for (row = raddatabaseResultsGetFirst (resultId);
         row != NULL;
         row = raddatabaseResultsGetNext (resultId))
    {
        field = raddatabaseFieldGet (row, "f1");
        if (field == NULL)
        {
            printf ("dbtest: raddatabaseFieldGet failed!\n");
            raddatabaseReleaseResults (dbId, resultId);
            raddatabaseClose (dbId);
            exit (1);
        }
        printf ("dbtest: f1=%d : ", raddatabaseFieldGetIntValue (field));
        
        field = raddatabaseFieldGet (row, "f2");
        if (field == NULL)
        {
            printf ("dbtest: raddatabaseFieldGet failed!\n");
            raddatabaseReleaseResults (dbId, resultId);
            raddatabaseClose (dbId);
            exit (1);
        }
        printf ("f2=%s\n", raddatabaseFieldGetCharValue (field));
    }
    raddatabaseReleaseResults (dbId, resultId);
    
    printf ("dbtest: done!\n");
    
    raddatabaseClose (dbId);
    radMsgLogExit ();
    exit (0);
}

