/*
 * Copyright 2001 Niels Provos <provos@citi.umich.edu>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Niels Provos.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include "config.h"

#include <md5.h>

#include "arc4.h"

/* 
 * An arc4 stream generator is used for encryption and pseudo-random
 * numbers.
 */

void
arc4_init(struct arc4_stream *as)
{
	int     n;

	for (n = 0; n < 256; n++)
		as->s[n] = n;
	as->i = 0;
	as->j = 0;
}

u_int8_t
arc4_getbyte(struct arc4_stream *as)
{
	u_int8_t si, sj;

	as->i = (as->i + 1);
	si = as->s[as->i];
	as->j = (as->j + si);
	sj = as->s[as->j];
	as->s[as->i] = sj;
	as->s[as->j] = si;
	return (as->s[(si + sj) & 0xff]);
}

void
arc4_skipbytes(struct arc4_stream *as, int skip)
{
	u_int8_t i, j, si, sj;
	u_int8_t *s;

	s = as->s;
	i = as->i;
	j = as->j;

	while (skip-- > 0) {
		i = (i + 1) & 0xff;
		si = s[i];
		j = (si + j) & 0xff;
		s[i] = sj = s[j];
		s[j] = si;
	}

	as->i = i;
	as->j = j;
}

u_int32_t
arc4_getword(struct arc4_stream *as)
{
	u_int32_t val;
	val = arc4_getbyte(as) << 24;
	val |= arc4_getbyte(as) << 16;
	val |= arc4_getbyte(as) << 8;
	val |= arc4_getbyte(as);
	return (val);
}

void
arc4_addrandom(struct arc4_stream *as, u_char *dat, int datlen)
{
        int     n;
        u_int8_t si, ki, i, j, *s;

	s = as->s;
        i = as->i - 1;
	j = as->j;
	ki = 0;
        for (n = 0; n < 256; n++) {
                i++;
                si = s[i];
                j = (j + si + dat[ki]);
                s[i] = s[j];
                s[j] = si;

		if (++ki >= datlen)
			ki = 0;
        }

	as->i = i;
	as->j = j;
}

void
arc4_initkey(struct arc4_stream *as, u_char *key, int keylen)
{
	MD5_CTX ctx;
	u_char digest[16];

	/* Bah, we want bcrypt */
	MD5Init(&ctx);
	MD5Update(&ctx, key, keylen);
	MD5Final(digest, &ctx);

	arc4_init(as);
	arc4_addrandom(as, digest, 16);
}

void
arc4_fixedkey(struct arc4_stream *as, u_char *key, int keylen)
{
	u_char digest[5];
	int i;

	memset(digest, 0, sizeof(digest));
	for (i = 0; i < keylen; i++)
		digest[i % 5] ^= key[i];
		
	arc4_init(as); 
	arc4_addrandom(as, digest, 5);

	/* Reset */
	as->i = as->j = 0;
}
