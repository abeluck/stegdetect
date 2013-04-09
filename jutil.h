/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
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
#ifndef _UTIL_H_
#define _UTIL_H_

void print_block(short *);

void dequant_block(short *, short *, JQUANT_TBL *);
void quant_block(short *, short *, JQUANT_TBL *);

void effective_change(short *, short *, short *, JQUANT_TBL *);
void effective_result(short *, short *, short *, JQUANT_TBL *);

int count_edge(short *);
int count_all(short *);

struct jeasy *jpeg_prepare_blocks(struct jpeg_decompress_struct *);
void jpeg_return_blocks(struct jeasy *, struct jpeg_decompress_struct *);
void jpeg_free_blocks(struct jeasy *);

void statistic(struct jeasy *);

double variance(short *);

int diff_vertical(short *, short *);
int diff_horizontal(short *, short *);

struct jeasy {
	int comp;
	int height[MAX_COMPS_IN_SCAN];
	int width[MAX_COMPS_IN_SCAN];
	struct jpeg_decompress_struct *jinfo;
	JQUANT_TBL *table[MAX_COMPS_IN_SCAN];
	short ***blocks;
	int needscale;
	double scale[MAX_COMPS_IN_SCAN];
};

#endif;
