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
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <errno.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <jpeglib.h>

#include "blowfish.h"
#include "bf_locl.h"
#include "break_jphide.h"
#include "config.h"
#include "common.h"

int break_jphide_v3(void *, BF_KEY *);
int break_jphide_v5(void *, BF_KEY *);

extern JBLOCKARRAY dctcompbuf[];

typedef u_int32_t blf_block[2];

#define NKSTREAMS 4

BF_KEY *ctx;
u_int prngn[NKSTREAMS];
blf_block prngstate[NKSTREAMS];

int coef, mode, spos;
int lh, lt, lw, where;
short *coeff;
int *lwib, *lhib;

#ifdef WORDS_BIGENDIAN
#define BLF_ENC(x,y) do { \
				u_char *tmp; \
				blf_block data; \
				tmp = (u_char *)&(x)[0]; \
				c2l(tmp, data[0]); \
				tmp = (u_char *)&(x)[1]; \
				c2l(tmp, data[1]); \
				BF_encrypt(data,y); \
				tmp = (u_char *)&(x)[0]; \
				l2c(data[0], tmp); \
				tmp = (u_char *)&(x)[1]; \
				l2c(data[1], tmp); \
			} while (0)
#define BLF_DEC(x,y) do { \
				u_char *tmp; \
				blf_block data; \
				tmp = (u_char *)&(x)[0]; \
				c2l(tmp, data[0]); \
				tmp = (u_char *)&(x)[1]; \
				c2l(tmp, data[1]); \
				BF_decrypt(data,y); \
				tmp = (u_char *)&(x)[0]; \
				l2c(data[0], tmp); \
				tmp = (u_char *)&(x)[1]; \
				l2c(data[1], tmp); \
			} while (0)
#else
#define BLF_ENC(x,y) BF_encrypt(x,y)
#define BLF_DEC(x,y) BF_decrypt(x,y)
#endif

#include "jphide_table.h"

struct jphobj {
	int bits;
	u_char iv[8];
	int wib[MAX_COMPS_IN_SCAN];
	int hib[MAX_COMPS_IN_SCAN];
	short coeff[256];
};

void *
break_jphide_read(char *filename)
{
	struct jphobj *job;
	int fd, i;

	fd = open(filename, O_RDONLY, 0);
	if (fd == -1) {
		fprintf(stderr, "%s : error: %s\n",
			filename, strerror(errno));
		return (NULL);
	}

	job = malloc(sizeof(struct jphobj));
	if (job == NULL)
		err(1, "malloc");

	if (read(fd, job, sizeof(*job)) != sizeof(*job)) {
		close(fd);
		free(job);
		return (NULL);
	}

	job->bits = ntohl(job->bits);

	for (i = 0; i < MAX_COMPS_IN_SCAN; i++) {
		job->wib[i] = ntohs(job->wib[i]);
		job->hib[i] = ntohs(job->hib[i]);
	}

	for (i = 0; i < sizeof(job->coeff)/sizeof(short); i++)
		job->coeff[i] = ntohs(job->coeff[i]);

	close(fd);

	return (job);
}

int
break_jphide_write(char *filename, void *arg)
{
	struct jphobj *job = arg;
	int fd, i;

	fd = open(filename, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		return (-1);

	job->bits = htonl(job->bits);

	for (i = 0; i < MAX_COMPS_IN_SCAN; i++) {
		job->wib[i] = htons(job->wib[i]);
		job->hib[i] = htons(job->hib[i]);
	}

	for (i = 0; i < sizeof(job->coeff)/sizeof(short); i++)
		job->coeff[i] = htons(job->coeff[i]);

	if (write(fd, job, sizeof(*job)) != sizeof(*job)) {
		close(fd);
		return (-1);
	}

	close(fd);

	return (0);
}

u_char
get_code_bit(int k)
{
	u_int8_t a;
	u_int32_t n;

	if ((n = prngn[k]++ & 0x3f) == 0)
		BLF_ENC(prngstate[k], ctx);

	a = ((u_char *)(prngstate[k]))[n >> 3] << (n & 0x07);

	return (a & 0x80 ? 1 : 0);
}

int
get_word(int *value)
{
	int y;

	while (1) {
		lw += 64;
		if (lw > lwib[coef]) {
			lh++;
			lw = spos;
			if (lh >= lhib[coef]) {
				lt += 3;
				if (ltab[lt] < 0) {
					return (1);
				}

				coef = ltab[lt];
				lh = 0;
				lw = spos = ltab[lt + 1];
				mode = ltab[lt + 2];
			}
		} 
		
		y = coeff[where++];

		if (coef == 0 && lh == 0 && (lw <= 7))
			continue;

		if (mode < 0) {
			if ((y >= mode) && (y <= -mode))
				continue;

			if (!get_code_bit(0) && !get_code_bit(0))
				continue;
		} else {
			if (mode == 3 && !get_code_bit(0))
				continue;

			if ((y >= -1) && (y <= 1)) {
				if (get_code_bit(0))
					continue;

				if (mode && get_code_bit(0))
					continue;
			}

			if (mode > 1 && !get_code_bit(0))
				continue;
		}

		*value = y;
		return (0);
	}
}

int
get_bit(void)
{
	int y;

	if (get_word(&y))
		return (-1);

	if (y < 0)
		y = 0 - y;

	if (mode < 0) {
		y &= 2;
		y >>= 1;
	} else
		y &= 1;

	return (y);
}

void
break_jphide_destroy(void *obj)
{
	free(obj);
}

int
break_jphide_compare(void *obj1, void *obj2)
{
	struct jphobj *job1 = obj1;
	struct jphobj *job2 = obj2;

	return (memcmp(job1->iv, job2->iv, 6));
}

void *
break_jphide_prepare(int bits)
{
	struct jphobj *job;
	int i;

	job = malloc(sizeof(struct jphobj));
	if (job == NULL)
		err(1, "malloc");

	job->bits = bits;
	for (i = 0; i < 3; i++) {
		job->wib[i] = 64 * wib[i] - 1;
		job->hib[i] = hib[i];
	}

	for (i = 0; i < 8; i++)
		job->iv[i] = dctcompbuf[0][0][0][i];

	coef = ltab [0];
	spos = ltab [1];
	lh = 0;
	lw = spos - 64;
	lt = 0;

	for(i = 0; i < sizeof(job->coeff)/sizeof(short); i++) {
		lw += 64;
		if (lw > job->wib[coef]) {
			lw = spos;
			lh++;
			if (lh >= job->hib[coef]) {
				lt += 3;
				if (ltab[lt] < 0)
					break;

				coef = ltab [lt];
				lw = spos = ltab [lt+1];
				lh = 0;
			}
		}

		job->coeff[i] = dctcompbuf[coef][lh][lw / 64][lw % 64];
	}

	return (job);
}

int
crack_jphide(char *filename, char *word, void *obj)
{
	static u_char iv[8];		/* version 5 marker */
	static u_char oword[57];	/* version 3 marker */
	static BF_KEY ctxv5;
	static BF_KEY ctxv3;
	static int initv5, initv3;
	struct jphobj *job = obj;
	int changed = 0;

	if (strcmp(word, oword)) {
		strlcpy(oword, word, sizeof(oword));
		changed = 1;
		initv3 = 0;
	}

	if (!initv5 || changed || memcmp(iv, job->iv, 6)) {
		u_char key[56];

		memcpy(iv, job->iv, sizeof(iv));
		memcpy(key, job->iv, 6);
		memcpy(key + 6, word, strlen(word));

		BF_set_key(&ctxv5, strlen(word) + 6, key);

		initv5 = 1;
	}

	if (break_jphide_v5(job, &ctxv5)) {
		fprintf(stdout, "%s : jphide[v5](%s)\n",
			filename, word);
		return (1);
	}

	if (!initv3 || changed) {
		BF_set_key(&ctxv3, strlen(word), word);
		
		initv3 = 1;
	}

	if (break_jphide_v3(job, &ctxv3)) {
		fprintf(stdout, "%s : jphide[v3](%s)\n",
			filename, word);
		return (1);
	}

	return (0);
}

void
break_jphide_setup(u_char *iv, struct jphobj *job, BF_KEY *inctx)
{
	int i;

	ctx = inctx;
	memcpy(iv, job->iv, sizeof(job->iv));

	memset(prngn, 0, sizeof(prngn));
	for (i = 0; i < NKSTREAMS; i++) {
		memcpy(prngstate + i, iv, 8);
		BLF_ENC(prngstate[i], ctx);

		iv[8] = iv[0]; 
		memmove(iv, iv + 1, 8);
	}

	coef = ltab [0];
	spos = ltab [1];
	mode = ltab [2];
	lh = 0;
	lw = spos - 64;
	lt = 0;

	lwib = job->wib;
	lhib = job->hib;
	coeff = job->coeff;
	where = 0;
}

int
break_jphide_getbytes(u_char *data, size_t len)
{
	int i, j, b;
	u_char v;

	for (i = 0; i < len; i++) {
		v = 0;
		for (j = 0; j < 8; j++) {
			if ((b = get_bit()) < 0)
				return (-1);

			b = b << j;
			v |= b;
		}
		data[i] = v;
	}

	return (0);
}

int
break_jphide_v3 (void *obj, BF_KEY *inctx)
{
	blf_block lendata;
	struct jphobj *job = obj;
	int i, len0, len1, len2, length;
	u_char iv[9];

	break_jphide_setup(iv, job, inctx);

	if (break_jphide_getbytes((u_char *)lendata, 8) == -1)
		return (0);

	BLF_DEC(lendata, ctx);

	len0 = ((u_char *)lendata)[0];
	len1 = ((u_char *)lendata)[1];
	len2 = ((u_char *)lendata)[2];
	
	length = (len0 << 16) | (len1 << 8) | len2;

	if (length * 8 >= job->bits)
		return (0);

	/* Encrypt IV for comparison with decryption */
	BLF_ENC((u_int32_t *)iv, ctx);

	for (i = 3; i < 8; i++)
		if (((u_char *)lendata)[i] != iv[i])
			return (0);

	return (1);
}

int
break_jphide_v5 (void *obj, BF_KEY *inctx)
{
	blf_block lendata[2];
	struct jphobj *job = obj;
	int len0, len1, len2, length;
	int rlen0, rlen1, rlen2, rlength;
	u_char iv[9], iv2[8], *p;

	break_jphide_setup(iv, job, inctx);

	if (break_jphide_getbytes((u_char *)lendata, 8) == -1)
		return (0);

	BLF_DEC(lendata[0], ctx);
	if (((u_char *)lendata)[3] > 3)
		return (0);

	len0 = ((u_char *)lendata)[0];
	len1 = ((u_char *)lendata)[1];
	len2 = ((u_char *)lendata)[2];
	
	length = (len0 << 16) | (len1 << 8) | len2;

	if (length * 8 >= job->bits)
		return (0);

	BLF_ENC((u_int32_t *)iv, ctx);

	p = (u_char *)lendata;

	if (memcmp(iv + 5, p + 5, 3))
		return (0);

	if (break_jphide_getbytes((u_char *)lendata[1], 8) == -1)
		return (0);

	BLF_DEC(lendata[1], ctx);

	if (((u_char *)lendata)[9] != len1 ||
	    ((u_char *)lendata)[10] != len2)
		return (0);

	rlen0 = ((unsigned char*)lendata)[4];
	rlen1 = ((unsigned char*)lendata)[8];
	rlen2 = ((unsigned char*)lendata)[11];

	rlength = (rlen0 << 16) | (rlen1 << 8) | rlen2;
	if (rlength && (rlength < length || rlength > 20 * length))
		return (0);

	memcpy(iv2, iv, sizeof(iv2));
	BLF_ENC((u_int32_t *)iv2, ctx);

	if (memcmp(iv2 + 4, p + 12, 4))
		return (0);

	return (1);
}
