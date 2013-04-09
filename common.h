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

#ifndef _COMMON_
#define _COMMON_

struct image {
	int x, y, depth, max;
	u_char *img;
};

void jpg_finish(void);
void jpg_destroy(void);
int jpg_open(char *);
void jpg_version(int *, int *, u_int16_t *);

int jpg_toimage(char *, struct image *);

int prepare_all(short **, int *);
int prepare_all_gradx(short **, int *);
int prepare_normal(short **, int *);
int prepare_jphide(short **, int *);
int prepare_jsteg(short **, int *);
int jsteg_size(short *, int, int *);
int prepare_outguess(short **, int *);

char *fgetl(char *, int, FILE *);
int file_hasextension(char *, char *);

int is_random(u_char *, int);

#define TEST_BIT(x,y)		((x)[(y) / 32] & (1 << ((y) & 31)))
#define WRITE_BIT(x,y,what)	((x)[(y) / 32] = ((x)[(y) / 32] & \
				~(1 << ((y) & 31))) | ((what) << ((y) & 31)))

extern int hib[], wib[];

enum order { ORDER_MCU, ORDER_NATURAL };

void stego_set_callback(void (*)(int, short), enum order);
void stego_set_eoi_callback(void (*cb)(void *));

#endif /* _COMMON_ */
