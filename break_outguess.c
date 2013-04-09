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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <jpeglib.h>
#include <file.h>

#include "config.h"
#include "common.h"
#include "arc4.h"
#include "break_outguess.h"

#ifndef MIN
#define		MIN(a,b) (((a)<(b))?(a):(b))
#endif

struct ogobj {
	int bits;
	u_int32_t coeff[4096];
};

#define DEFAULT_ITER	256
#define INIT_SKIPMOD	32
#define SKIPADJ(x,y)	((y) > (x)/32 ? 2 : 2 - ((x/32) - (y))/(float)(x/32))

typedef struct _iterator {
	struct arc4_stream as;
	u_int32_t skipmod;
	int off;		/* Current bit position */
} iterator;

int break_outguess(struct ogobj *, struct arc4_stream *, iterator *,
    char **, int *);

/* Globals */
int min_len = 256;
int max_seed = 55000;

void
iterator_init(iterator *iter, struct arc4_stream *as)
{
	int i;
	char derive[16];

	iter->skipmod = INIT_SKIPMOD;
	iter->as = *as;

	/* Put the PRNG in different state, using key dependant data
	 * provided by the PRNG itself.
	 */
	for (i = 0; i < sizeof(derive); i++)
		derive[i] = arc4_getbyte(&iter->as);
	arc4_addrandom(&iter->as, derive, sizeof(derive));

	iter->off = arc4_getword(&iter->as) % iter->skipmod;
}

#define iterator_current(x)	(x)->off

int
iterator_next(iterator *iter)
{
	iter->off += (arc4_getword(&iter->as) % iter->skipmod) + 1;

	return (iter->off);
}

void
iterator_seed(iterator *iter, u_int16_t seed)
{
	u_int8_t reseed[2];

	reseed[0] = seed;
	reseed[1] = seed >> 8;

	arc4_addrandom(&iter->as, reseed, 2);
}

void
iterator_adapt(iterator *iter, int bits, int datalen)
{
	iter->skipmod = SKIPADJ(bits, bits - iter->off) *
	    (bits - iter->off)/(8 * datalen);
}

u_int32_t
steg_retrbyte(u_int32_t *bitmap, int bits, iterator *iter)
{
	u_int32_t i = iterator_current(iter);
	u_int32_t tmp = 0;
	int where;

	for (where = 0; where < bits; where++) {
		tmp |= (TEST_BIT(bitmap, i) != 0) << where;

		i = iterator_next(iter);
	}

	return (tmp);
}

void *
break_outguess_read(char *filename)
{
	struct ogobj *ogob;
	int fd, i;

	fd = open(filename, O_RDONLY, 0);
	if (fd == -1) {
		fprintf(stderr, "%s : error: %s\n",
			filename, strerror(errno));
		return (NULL);
	}

	ogob = malloc(sizeof(struct ogobj));
	if (ogob == NULL)
		err(1, "malloc");

	if (read(fd, ogob, sizeof(*ogob)) != sizeof(*ogob)) {
		close(fd);
		free(ogob);
		return (NULL);
	}

	ogob->bits = ntohl(ogob->bits);
	for (i = 0; i < sizeof(ogob->coeff)/sizeof(u_int32_t); i++)
		ogob->coeff[i] = ntohl(ogob->coeff[i]);

	close(fd);

	return (ogob);
}

int
break_outguess_write(char *filename, void *arg)
{
	struct ogobj *ogob = arg;
	int fd, i;

	fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		return (-1);

	ogob->bits = htonl(ogob->bits);
	for (i = 0; i < sizeof(ogob->coeff)/sizeof(u_int32_t); i++)
		ogob->coeff[i] = htonl(ogob->coeff[i]);

	if (write(fd, ogob, sizeof(*ogob)) != sizeof(*ogob)) {
		close(fd);
		return (-1);
	}

	close(fd);

	return (0);
}

void
break_outguess_destroy(void *obj)
{
	free(obj);
}

void *
break_outguess_prepare(short *dcts, int bits)
{
	struct ogobj *ogob;
	int i, j, max;
	short val;
	
	ogob = malloc(sizeof(struct ogobj));
	if (ogob == NULL)
		err(1, "malloc");

	ogob->bits = bits;

	max = sizeof(ogob->coeff) * 8;
	j = 0;
	for (i = 0; i < bits && j < max; i++) {
		val = dcts[i];

		WRITE_BIT(ogob->coeff, j, (val & 0x01));
		j++;
	}
	
	return (ogob);
}

int
crack_outguess(char *filename, char *word, void *obj)
{
	static u_char oword[57];	/* version 3 marker */
	static int init;
	static struct arc4_stream as;
	static iterator it;
	char *buf;
	int buflen;
	
	struct arc4_stream tas;
	iterator tit;
	struct ogobj *ogob = obj;
	int changed = 0;

	if (strcmp(word, oword)) {
		strlcpy(oword, word, sizeof(oword));
		changed = 1;
		init = 0;
	}

	if (!init || changed) {
		arc4_initkey(&as, word, strlen(word));
		iterator_init(&it, &as);
		init = 1;
	}

	tas = as;
	tit = it;
	if (break_outguess(ogob, &tas, &tit, &buf, &buflen)) {
		int i;
		extern int noprint;
		fprintf(stdout, "%s : outguess[v0.13b](%s)[",
			filename, word);
		noprint = 0;
		file_process(buf, buflen);
		noprint = 1;
		fprintf(stdout, "][");
		for (i = 0; i < 16; i++)
			fprintf(stdout, "%c",
			    isprint(buf[i]) ? buf[i] : '.');
		fprintf(stdout, "]\n");
		return (1);
	}

	return (0);
}

int
break_outguess(struct ogobj *og, struct arc4_stream *as, iterator *it,
    char **pbuf, int *pbuflen)
{
	u_char state[4];
	static u_char buf[512];
	struct arc4_stream tas = *as;
	int length, seed, need;
	int bits, i, n;

	state[0] = steg_retrbyte(og->coeff, 8, it) ^ arc4_getbyte(as);
	state[1] = steg_retrbyte(og->coeff, 8, it) ^ arc4_getbyte(as);
	state[2] = steg_retrbyte(og->coeff, 8, it) ^ arc4_getbyte(as);
	state[3] = steg_retrbyte(og->coeff, 8, it) ^ arc4_getbyte(as);

	seed = (state[1] << 8) | state[0];
	length = (state[3] << 8) | state[2];

	if (seed > max_seed || length * 8 >= og->bits/2 || length < min_len)
		return (0);

	iterator_seed(it, seed);

	bits = MIN(og->bits, sizeof(og->coeff) * 8);

	n = 0;
	while (iterator_current(it) < bits && length > 0 && n < sizeof(buf)) {
		iterator_adapt(it, og->bits, length);
		buf[n++] = steg_retrbyte(og->coeff, 8, it);
		length--;
	}

	/* For testing the randomness, we need some extra information */
	need = MIN(min_len, sizeof(buf));
	if (n < need || !is_random(buf, n))
		return (0);

	/* Plaintext tests? */
	for (i = 0; i < n; i++)
		buf[i] ^= arc4_getbyte(&tas);

	if (file_process(buf, n) == 0)
		return (0);

	*pbuf = buf;
	*pbuflen = n;

	return (1);
}
