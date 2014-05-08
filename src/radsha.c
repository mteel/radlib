/*---------------------------------------------------------------------------
 
  FILENAME:
        radsha.h
 
  PURPOSE:
        Provide SHA-1 and SHA-256 secure hashing algorithm utilities.
 
  REVISION HISTORY:
        Date            Engineer        Revision        Remarks
        10/20/2005      M.S. Teel       0               Original
 
  NOTES:
        These utilities generate unique secure hashes or "digests" for given
        memory blocks or files.
 
        SHA-1       20-byte digest
        SHA-256     32-byte digest
 
        The SHA algorithms were designed by the National Security Agency (NSA) 
        and published as US government standards.
 
        The SHA-256 implementation is derived from the FreeBSD implementation
        written by Aaron D. Gifford.
 
  LICENSE:
        Portions are Copyright 2000 Aaron D. Gifford. All rights reserved.
        Copyright 2005 Mark S. Teel. All rights reserved.
 
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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

/*  ... Library include files
*/
#include <radsysdefs.h>
#include <radsysutils.h>
#include <radsha.h>


/*** SHA-256 Various Length Definitions ***********************/
/* NOTE: Most of these are in sha2.h */
#define RADSHA256_SHORT_BLOCK_LENGTH   (RADSHA256_BLOCK_LENGTH - 8)


/*** ENDIAN REVERSAL MACROS *******************************************/
#ifndef WORDS_BIGENDIAN
#define REVERSE32(w,x) { \
     ULONG tmp = (w); \
     tmp = (tmp >> 16) | (tmp << 16); \
     (x) = ((tmp & 0xff00ff00UL) >> 8) | ((tmp & 0x00ff00ffUL) << 8); \
}
#define REVERSE64(w,x) { \
     ULONGLONG tmp = (w); \
     tmp = (tmp >> 32) | (tmp << 32); \
     tmp = ((tmp & 0xff00ff00ff00ff00ULL) >> 8) | \
           ((tmp & 0x00ff00ff00ff00ffULL) << 8); \
     (x) = ((tmp & 0xffff0000ffff0000ULL) >> 16) | \
           ((tmp & 0x0000ffff0000ffffULL) << 16); \
}
#endif

/*
 * Macro for incrementally adding the unsigned 64-bit integer n to the
 * unsigned 128-bit integer (represented using a two-element array of
 * 64-bit words):
 */
#define ADDINC128(w,n) { \
     (w)[0] += (ULONGLONG)(n); \
     if ((w)[0] < (n)) { \
      (w)[1]++; \
     } \
}

/*** THE SIX LOGICAL FUNCTIONS ****************************************/
/*
 * Bit shifting and rotation (used by the six SHA-XYZ logical functions:
 *
 *   NOTE:  The naming of R and S appears backwards here (R is a SHIFT and
 *   S is a ROTATION) because the SHA-256/384/512 description document
 *   (see http://csrc.nist.gov/cryptval/shs/sha256-384-512.pdf) uses this
 *   same "backwards" definition.
 */
/* Shift-right (used in SHA-256, SHA-384, and SHA-512): */
#define R(b,x)   ((x) >> (b))
/* 32-bit Rotate-right (used in SHA-256): */
#define S32(b,x) (((x) >> (b)) | ((x) << (32 - (b))))

/* Two of six logical functions used in SHA-256, SHA-384, and SHA-512: */
#define Ch(x,y,z) (((x) & (y)) ^ ((~(x)) & (z)))
#define Maj(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

/* Four of six logical functions used in SHA-256: */
#define Sigma0_256(x) (S32(2,  (x)) ^ S32(13, (x)) ^ S32(22, (x)))
#define Sigma1_256(x) (S32(6,  (x)) ^ S32(11, (x)) ^ S32(25, (x)))
#define sigma0_256(x) (S32(7,  (x)) ^ S32(18, (x)) ^ R(3 ,   (x)))
#define sigma1_256(x) (S32(17, (x)) ^ S32(19, (x)) ^ R(10,   (x)))

/*** SHA-XYZ INITIAL HASH VALUES AND CONSTANTS ************************/
/* Hash constant words K for SHA-256: */
const static ULONG K256[64] =
{
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

/* Initial hash value H for SHA-256: */
const static ULONG sha256_initial_hash_value[8] =
{
    0x6a09e667UL,
    0xbb67ae85UL,
    0x3c6ef372UL,
    0xa54ff53aUL,
    0x510e527fUL,
    0x9b05688cUL,
    0x1f83d9abUL,
    0x5be0cd19UL
};

/*
 * Constant used by SHA256/384/512_End() functions for converting the
 * digest to a readable hexadecimal character string:
 */
static const char *sha_hex_digits = "0123456789abcdef";

static void SHA1Init (struct SHA1Context *context)
{
    int         i;

    context->h0 = 0x67452301L;
    context->h1 = 0xefcdab89L;
    context->h2 = 0x98badcfeL;
    context->h3 = 0x10325476L;
    context->h4 = 0xc3d2e1f0L;
    context->hi_length = 0;
    context->lo_length = 0;
    context->inbytes = 0;
    context->padded = FALSE;

    for (i = 0; i < 80; i ++)
    {
        context->in.W[i] = 0L;
    }
}

#ifndef WORDS_BIGENDIAN
/* When run on a little-endian CPU we need to perform byte reversal on an
   array of longwords.  It is possible to make the code endianness-
   independant by fiddling around with data at the byte level, but this
   makes for very slow code, so we rely on the user to sort out endianness
   at compile time */

static void ulongReverse (ULONG *buffer, int ulongCount)
{
    ULONG       value;
    int         count;

    for (count = 0; count < ulongCount; count ++)
    {
        value = ( buffer[count] << 16 ) | ( buffer[count] >> 16 );
        buffer[count] = ( ( value & 0xFF00FF00L ) >> 8 ) | ( ( value & 0x00FF00FFL ) << 8 );
    }
}
#endif

#define f0(x,y,z) (z ^ (x & (y ^ z)))           /* Magic functions */
#define f1(x,y,z) (x ^ y ^ z)
#define f2(x,y,z) ((x & y) | (z & (x | y)))
#define f3(x,y,z) (x ^ y ^ z)

#define K0 0x5a827999                           /* Magic constants */
#define K1 0x6ed9eba1
#define K2 0x8f1bbcdc
#define K3 0xca62c1d6

#define S(n, X) ((X << n) | (X >> (32 - n)))    /* Barrel roll */

#define r0(f, K) \
    temp = S(5, A) + f(B, C, D) + E + *p0++ + K; \
    E = D;  \
    D = C;  \
    C = S(30, B); \
    B = A;  \
    A = temp

#ifdef VERSION_0
#define r1(f, K) \
    temp = S(5, A) + f(B, C, D) + E + \
    (*p0++ = *p1++ ^ *p2++ ^ *p3++ ^ *p4++) + K; \
    E = D;  \
    D = C;  \
    C = S(30, B); \
    B = A;  \
    A = temp
#else                   /* Version 1: Summer '94 update */
#define r1(f, K) \
    temp = *p1++ ^ *p2++ ^ *p3++ ^ *p4++; \
    temp = S(5, A) + f(B, C, D) + E + (*p0++ = S(1,temp)) + K; \
    E = D;  \
    D = C;  \
    C = S(30, B); \
    B = A;  \
    A = temp
#endif

/* This is the guts of the SHA1 calculation.  If we can get 64 bytes into
   the holding context->in buffer, we go ahead and process these bytes,
   adding them to the accumulated message digest. If not, and finalize is
   false, we just store however many bytes we can get for future use.
   If finalize is true, we pad this block to 64 bytes and try to fit in the
   accumulated bit length of the message (which we can do only if the
   original block < 56 bytes). We return TRUE if we need to add one more
   block to fit in the bit length. (Returned value is only significant if
   finalize is true.) The pointer to the input buffer is advanced and its
   length is updated in every case. */

static int SHA1Update0
(
    struct SHA1Context  *context,
    UCHAR const         **buf,
    unsigned int        *len,
    int                 finalize
)
{
    int                 i, nread, nbits;
    char                *s;
    register ULONG      *p0, *p1, *p2, *p3, *p4;
    ULONG               A, B, C, D, E, temp;

    if (*len < 64 - context->inbytes)
    {
        if (*len)
        {
            memcpy (context->in.B + context->inbytes, *buf, *len);
            *buf += *len;
            context->inbytes += *len;
            *len = 0;
        }
        if (!finalize)
            return TRUE;

        nread = context->inbytes;
        context->inbytes = 0;
        nbits = nread << 3;               /* Length: bits */
        if ((context->lo_length += nbits) < nbits)
            context->hi_length++;              /* 64-bit integer */

        if (! context->padded)  /* Append a single bit */
        {
            context->in.B[nread++] = 0x80; /* Using up next byte */
            context->padded = TRUE;       /* Single bit once */
        }
        for (i = nread; i < 64; i ++) /* Pad with nulls */
            context->in.B[i] = 0;
        if (nread <= 56)   /* Room for length in this block */
        {
            context->in.W[14] = context->hi_length;
            context->in.W[15] = context->lo_length;
#ifndef WORDS_BIGENDIAN
            ulongReverse (context->in.W, 56/sizeof(ULONG) );
#endif

        }
#ifndef WORDS_BIGENDIAN
        else
            ulongReverse (context->in.W, 64/sizeof(ULONG) );
#endif

    }
    else
    {
        memcpy (context->in.B + context->inbytes, *buf, 64 - context->inbytes);
        *buf += 64 - context->inbytes;
        *len -= 64 - context->inbytes;
        context->inbytes = 0;
        if ((context->lo_length += 512) < 512)
            context->hi_length++;    /* 64-bit integer */
#ifndef WORDS_BIGENDIAN
        ulongReverse (context->in.W, 64/sizeof(ULONG) );
#endif

    }
    p0 = context->in.W;
    A = context->h0;
    B = context->h1;
    C = context->h2;
    D = context->h3;
    E = context->h4;

    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);
    r0(f0,K0);

    p1 = &context->in.W[13];
    p2 = &context->in.W[8];
    p3 = &context->in.W[2];
    p4 = &context->in.W[0];

    r1(f0,K0);
    r1(f0,K0);
    r1(f0,K0);
    r1(f0,K0);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f1,K1);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f2,K2);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);
    r1(f3,K3);

    context->h0 += A;
    context->h1 += B;
    context->h2 += C;
    context->h3 += D;
    context->h4 += E;
    return nread > 56;
}

/* Add one buffer's worth of bytes to the SHA1 calculation. If the number of
   bytes in the buffer is not a multiple of 64, hold the bytes in the last
   partial block in the context->in holding area for the next time this
   routine is called. */
static void SHA1Update
(
    struct SHA1Context  *context,
    UCHAR const         *buf,
    unsigned int        len
)
{
    unsigned int        len2 = len;
    UCHAR const         *buf2 = buf;

    while (len2)
    {
        SHA1Update0 (context, &buf2, &len2, FALSE);
    }
}

/* Finish off the SHA1 calculation by adding in the left over bytes in
   context->in with padding and with the total bit length of the message.
   May have to call SHA1Update0() twice, if there isn't enough room to
   process the bit length the first time around. */
static void SHA1Final
(
    struct SHA1Context  *context,
    char                *digest
)
{
    unsigned int        len = 0L;
    ULONG               *p = (ULONG *)digest;

    while (SHA1Update0 (context, (void *)NULL, &len, TRUE));
    p[0] = context->h0;
    p[1] = context->h1;
    p[2] = context->h2;
    p[3] = context->h3;
    p[4] = context->h4;

#ifndef WORDS_BIGENDIAN
    ulongReverse((ULONG *)digest, 5);
#endif

    memset (context, 0, sizeof(context));   /* In case it's sensitive */
}

static void SHA1_End (struct SHA1Context *context, char buffer[]) 
{
    UCHAR       digest[RADSHA1_DIGEST_LENGTH], *d = digest;
    int         i;

    if (buffer != (char*)0)
    {
        SHA1Final (context, (char *)digest);

        for (i = 0; i < RADSHA1_DIGEST_LENGTH; i++)
        {
            *buffer++ = sha_hex_digits[(*d & 0xf0) >> 4];
            *buffer++ = sha_hex_digits[*d & 0x0f];
            d++;
        }
        *buffer = (char)0;
    }
    else
    {
        memset (context, 0, sizeof(context));
    }
    memset (digest, 0, RADSHA1_DIGEST_LENGTH);
    return;
}


/*** SHA-256: *********************************************************/
static void SHA256_Init (SHA256_CTX *context)
{
    if (context == (SHA256_CTX *)0)
    {
        return;
    }
    memcpy (context->state, sha256_initial_hash_value, RADSHA256_DIGEST_LENGTH);
    memset (context->buffer, 0, RADSHA256_BLOCK_LENGTH);
    context->bitcount = 0;
}

/* Unrolled SHA-256 round macros: */

#ifndef WORDS_BIGENDIAN

#define ROUND256_0_TO_15(a,b,c,d,e,f,g,h) \
     REVERSE32(*data++, W256[j]); \
     T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + \
                 K256[j] + W256[j]; \
     (d) += T1; \
     (h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
     j++


#else

#define ROUND256_0_TO_15(a,b,c,d,e,f,g,h) \
     T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + \
          K256[j] + (W256[j] = *data++); \
     (d) += T1; \
     (h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
     j++

#endif

#define ROUND256(a,b,c,d,e,f,g,h) \
     s0 = W256[(j+1)&0x0f]; \
     s0 = sigma0_256(s0); \
     s1 = W256[(j+14)&0x0f]; \
     s1 = sigma1_256(s1); \
     T1 = (h) + Sigma1_256(e) + Ch((e), (f), (g)) + K256[j] + \
          (W256[j&0x0f] += s1 + W256[(j+9)&0x0f] + s0); \
     (d) += T1; \
     (h) = T1 + Sigma0_256(a) + Maj((a), (b), (c)); \
     j++

static void SHA256_Transform (SHA256_CTX *context, const ULONG *data) 
{
    ULONG       a, b, c, d, e, f, g, h, s0, s1;
    ULONG       T1, *W256;
    int         j;

    W256 = (ULONG *)context->buffer;

    /* Initialize registers with the prev. intermediate value */
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    j = 0;
    do
    {
        /* Rounds 0 to 15 (unrolled): */
        ROUND256_0_TO_15(a,b,c,d,e,f,g,h);
        ROUND256_0_TO_15(h,a,b,c,d,e,f,g);
        ROUND256_0_TO_15(g,h,a,b,c,d,e,f);
        ROUND256_0_TO_15(f,g,h,a,b,c,d,e);
        ROUND256_0_TO_15(e,f,g,h,a,b,c,d);
        ROUND256_0_TO_15(d,e,f,g,h,a,b,c);
        ROUND256_0_TO_15(c,d,e,f,g,h,a,b);
        ROUND256_0_TO_15(b,c,d,e,f,g,h,a);
    }
    while (j < 16);

    /* Now for the remaining rounds to 64: */
    do
    {
        ROUND256(a,b,c,d,e,f,g,h);
        ROUND256(h,a,b,c,d,e,f,g);
        ROUND256(g,h,a,b,c,d,e,f);
        ROUND256(f,g,h,a,b,c,d,e);
        ROUND256(e,f,g,h,a,b,c,d);
        ROUND256(d,e,f,g,h,a,b,c);
        ROUND256(c,d,e,f,g,h,a,b);
        ROUND256(b,c,d,e,f,g,h,a);
    }
    while (j < 64);

    /* Compute the current intermediate hash value */
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;

    /* Clean up */
    a = b = c = d = e = f = g = h = T1 = 0;
}

static void SHA256_Update (SHA256_CTX *context, const UCHAR *data, size_t len) 
{
    unsigned int    freespace, usedspace;

    if (len == 0)
    {
        /* Calling with no data is valid - we do nothing */
        return;
    }

    usedspace = (context->bitcount >> 3) % RADSHA256_BLOCK_LENGTH;
    if (usedspace > 0)
    {
        /* Calculate how much free space is available in the buffer */
        freespace = RADSHA256_BLOCK_LENGTH - usedspace;

        if (len >= freespace)
        {
            /* Fill the buffer completely and process it */
            memcpy (&context->buffer[usedspace], data, freespace);
            context->bitcount += freespace << 3;
            len -= freespace;
            data += freespace;
            SHA256_Transform (context, (ULONG*)context->buffer);
        }
        else
        {
            /* The buffer is not yet full */
            memcpy (&context->buffer[usedspace], data, len);
            context->bitcount += len << 3;
            /* Clean up: */
            usedspace = freespace = 0;
            return;
        }
    }
    while (len >= RADSHA256_BLOCK_LENGTH)
    {
        /* Process as many complete blocks as we can */
        SHA256_Transform (context, (const ULONG*)data);
        context->bitcount += RADSHA256_BLOCK_LENGTH << 3;
        len -= RADSHA256_BLOCK_LENGTH;
        data += RADSHA256_BLOCK_LENGTH;
    }
    if (len > 0)
    {
        /* There's left-overs, so save 'em */
        memcpy (context->buffer, data, len);
        context->bitcount += len << 3;
    }
    /* Clean up: */
    usedspace = freespace = 0;
}

static void SHA256_Final (UCHAR digest[], SHA256_CTX *context) 
{
    ULONG           *d = (ULONG *)digest;
    unsigned int    usedspace;

    /* If no digest buffer is passed, we don't bother doing this: */
    if (digest != (UCHAR *)0)
    {
        usedspace = (context->bitcount >> 3) % RADSHA256_BLOCK_LENGTH;
#ifndef WORDS_BIGENDIAN
        /* Convert FROM host byte order */
        REVERSE64(context->bitcount,context->bitcount);
#endif

        if (usedspace > 0)
        {
            /* Begin padding with a 1 bit: */
            context->buffer[usedspace++] = 0x80;

            if (usedspace < RADSHA256_SHORT_BLOCK_LENGTH)
            {
                /* Set-up for the last transform: */
                memset (&context->buffer[usedspace], 0, RADSHA256_SHORT_BLOCK_LENGTH - usedspace);
            }
            else
            {
                if (usedspace < RADSHA256_BLOCK_LENGTH)
                {
                    memset (&context->buffer[usedspace], 0, RADSHA256_BLOCK_LENGTH - usedspace);
                }
                /* Do second-to-last transform: */
                SHA256_Transform (context, (ULONG *)context->buffer);

                /* And set-up for the last transform: */
                memset (context->buffer, 0, RADSHA256_SHORT_BLOCK_LENGTH);
            }
        }
        else
        {
            /* Set-up for the last transform: */
            memset (context->buffer, 0, RADSHA256_SHORT_BLOCK_LENGTH);

            /* Begin padding with a 1 bit: */
            *context->buffer = 0x80;
        }
        /* Set the bit count: */
        *(ULONGLONG *)&context->buffer[RADSHA256_SHORT_BLOCK_LENGTH] = context->bitcount;

        /* Final transform: */
        SHA256_Transform (context, (ULONG *)context->buffer);

#ifndef WORDS_BIGENDIAN

        {
            /* Convert TO host byte order */
            int j;
            for (j = 0; j < 8; j++)
            {
                REVERSE32(context->state[j],context->state[j]);
                *d++ = context->state[j];
            }
        }
#else
        memcpy (d, context->state, RADSHA256_DIGEST_LENGTH);
#endif

    }

    /* Clean up state data: */
    memset (context, 0, sizeof(context));
    usedspace = 0;
}

static void SHA256_End (SHA256_CTX *context, char buffer[]) 
{
    UCHAR       digest[RADSHA256_DIGEST_LENGTH], *d = digest;
    int         i;

    if (buffer != (char *)0)
    {
        SHA256_Final (digest, context);

        for (i = 0; i < RADSHA256_DIGEST_LENGTH; i++)
        {
            *buffer++ = sha_hex_digits[(*d & 0xf0) >> 4];
            *buffer++ = sha_hex_digits[*d & 0x0f];
            d++;
        }
        *buffer = (char)0;
    }
    else
    {
        memset (context, 0, sizeof(context));
    }
    memset (digest, 0, RADSHA256_DIGEST_LENGTH);
    return;
}


/*  ... API methods
*/
/*	... Compute the SHA-1 digest for the memory block 'block' of byte length
	... 'byteLength' and store the result in 'digestStore'
    ... returns OK or ERROR
*/
int radSHA1ComputeBlock 
(
    void                *block, 
    int                 byteLength, 
    char                digestStore[RADSHA1_DIGEST_STR_LENGTH]
)
{
    struct SHA1Context  context;

    memset (digestStore, 0, RADSHA1_DIGEST_STR_LENGTH);

    SHA1Init (&context);
    SHA1Update (&context, (UCHAR *)block, byteLength);
    SHA1_End (&context, digestStore);

    return OK;
}


/*	... Compute the SHA-1 digest for the file given by full path and filename 
    ... 'filename' and store the result in 'digestStore'
    ... returns OK or ERROR
*/
int radSHA1ComputeFile
(
    char                *filename, 
    char                digestStore[RADSHA1_DIGEST_STR_LENGTH]
)
{
    struct SHA1Context  context;
    FILE                *inFile;
    int                 bytes;
    UCHAR               buffer[1024];

    memset (digestStore, 0, RADSHA1_DIGEST_STR_LENGTH);

    inFile = fopen (filename, "rb");
    if (inFile == NULL)
    {
        return ERROR;
    }

    SHA1Init (&context);

    while ((bytes = fread (buffer, 1, 1024, inFile)) != 0)
    {
        SHA1Update (&context, buffer, bytes);
    }

    fclose (inFile);

    SHA1_End (&context, digestStore);

    return OK;
}

/*	... Compute the SHA-256 digest for the memory block 'block' of byte length
	... 'byteLength' and store the result in 'digestStore'
    ... returns OK or ERROR
*/
int radSHA256ComputeBlock 
(
    void            *block, 
    int             byteLength, 
    char            digestStore[RADSHA256_DIGEST_STR_LENGTH]
)
{
    SHA256_CTX      context;

    memset (digestStore, 0, RADSHA256_DIGEST_STR_LENGTH);

    SHA256_Init (&context);
    SHA256_Update (&context, (UCHAR *)block, byteLength);
    SHA256_End (&context, digestStore);

    return OK;
}


/*	... Compute the SHA-256 digest for the file given by full path and filename 
    ... 'filename' and store the result in 'digestStore'
    ... returns OK or ERROR
*/
int radSHA256ComputeFile
(
    char            *filename, 
    char            digestStore[RADSHA256_DIGEST_STR_LENGTH]
)
{
    SHA256_CTX      context;
    FILE            *inFile;
    int             bytes;
    UCHAR           buffer[1024];

    memset (digestStore, 0, RADSHA256_DIGEST_STR_LENGTH);

    inFile = fopen (filename, "rb");
    if (inFile == NULL)
    {
        return ERROR;
    }

    SHA256_Init (&context);

    while ((bytes = fread (buffer, 1, 1024, inFile)) != 0)
    {
        SHA256_Update (&context, buffer, bytes);
    }

    fclose (inFile);

    SHA256_End (&context, digestStore);

    return OK;
}

