#ifndef INC__pgtypesh
#define INC__pgtypesh
/*---------------------------------------------------------------------------
 
  FILENAME:
        _pg_types.h
 
  PURPOSE:
        Provide PostgreSQL built-in data types we support.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        06/10/2005      M.S. Teel       0               Original
 
  NOTES:
        Based on the definitions in catalogue/pg_type.h
 
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

/*  ... System include files
*/

/*  ... Library include files
*/
#include <radsysdefs.h>


typedef union
{
    char        *ch;
    UCHAR       *int1;
    short       *int2;
    int         *int4;
    long long   *int8;
    float       *fl;
    double      *dfl;
} PG_PTR;


#define FIELD_TYPE_TINY         16            // 1 byte - int
#define FIELD_TYPE_SHORT        21            // 2 bytes - int
#define FIELD_TYPE_INT          23            // 4 bytes - int
#define FIELD_TYPE_LONGLONG     20            // 8 bytes - longlong
#define FIELD_TYPE_STRING       1042          // fixed length - string
#define FIELD_TYPE_FLOAT        700           // 4 bytes - float
#define FIELD_TYPE_DOUBLE       701           // 8 bytes - double
#define FIELD_TYPE_DATETIME     1114          // 8 bytes - time stamp


#endif

