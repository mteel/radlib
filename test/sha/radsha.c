#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <radsysdefs.h>
#include <radsha.h>


int main (int argc, char *argv[])
{
    int              is256;
    char             digest[RADSHA256_DIGEST_STR_LENGTH];

    if (argc < 3)
    {
        printf ("\nUsage: radsha [1|256] [filename]\n");
        return 1;
    }

    is256 = atoi (argv[1]);

    if (is256 == 256)
    {
        if (radSHA256ComputeFile (argv[2], digest) == ERROR)
        {
            printf ("\nCould not open file: %s\n", argv[2]);
            return 1;
        }
    }
    else
    {
        if (radSHA1ComputeFile (argv[2], digest) == ERROR)
        {
            printf ("\nCould not open file: %s\n", argv[2]);
            return 1;
        }
    }

    if (is256 == 256)
    {
        printf ("SHA-256 (%s): ", argv[2]);
    }
    else
    {
        printf ("SHA-1 (%s): ", argv[2]);
    }

    printf ("%s\n", digest);
    return 0;
}
