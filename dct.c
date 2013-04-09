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
#include <sys/types.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <err.h>

#include <jpeglib.h>

#include "dct.h"

static int dct_inited;
static double **D, **Dt;
static double **tmp1, **tmp2;

static double _D[8][8] = {
	{0.35355339059327, 0.35355339059327, 0.35355339059327, 0.35355339059327, 0.35355339059327, 0.35355339059327, 0.35355339059327, 0.35355339059327},

	{0.49039264020162, 0.41573480615127, 0.27778511650980, 0.09754516100806, -0.09754516100806, -0.27778511650980, -0.41573480615127, -0.49039264020162},

	{0.46193976625564, 0.19134171618254, -0.19134171618254, -0.46193976625564, -0.46193976625564, -0.19134171618255, 0.19134171618255, 0.46193976625564},

	{0.41573480615127, -0.09754516100806, -0.49039264020162, -0.27778511650980, 0.27778511650980, 0.49039264020162, 0.09754516100806, -0.41573480615127},

	{0.35355339059327, -0.35355339059327, -0.35355339059327, 0.35355339059327, 0.35355339059327, -0.35355339059327, -0.35355339059327, 0.35355339059327},

	{0.27778511650980, -0.49039264020162, 0.09754516100806, 0.41573480615127, -0.41573480615127, -0.09754516100806, 0.49039264020162, -0.27778511650980},

	{0.19134171618254, -0.46193976625564, 0.46193976625564, -0.19134171618254, -0.19134171618255, 0.46193976625564, -0.46193976625564, 0.19134171618254},

	{0.09754516100806, -0.27778511650980, 0.41573480615127, -0.49039264020162, 0.49039264020162, -0.41573480615127, 0.27778511650980, -0.09754516100806}
};

void
mat_transpose(double **out, double **in, int size)
{
	int i, j;

	for (i = 0; i < size; i++)
		for (j = 0; j < size; j++)
			out[i][j] = in[j][i];
}

double **
mat_new(int size)
{
	double **mat;
	int i;

	if ((mat = malloc(sizeof(double) * size)) == NULL)
		err(1, "malloc");

	for (i = 0; i < size; i++) {
		if ((mat[i] = calloc(size, sizeof(double))) == NULL)
			err(1, "calloc");
	}

	return (mat);
}

void
mat_mul(double **out, double **a, double **b, int size)
{
	int i, j, k;

	for (i = 0; i < size; i++) {
		for (j = 0; j < size; j++) {
			double sum = 0;

			for (k = 0; k < size; k++)
				sum += a[i][k]*b[k][j];

			out[i][j] = sum;
		}
	}
}

void
mat_sadd(double **out, double **in, double val, int size)
{
	int i, j;

	for (i = 0; i < size; i++)
		for (j = 0; j < size; j++)
			out[i][j] = in[i][j] + val;

}

void
dcttomat(double **mat, short *dct)
{
	int i;

	for (i = 0; i < DCTSIZE2; i++) {
		mat[i >> 3][i & 7] = dct[i];
	}
}

void
mattodct(short *dct, double **mat)
{
	int i;

	for (i = 0; i < DCTSIZE2; i++) {
		dct[i] = mat[i >> 3][i & 7];
	}
}

void
dct_init(void)
{
	int i, j;

	D = mat_new(DCTSIZE);
	Dt = mat_new(DCTSIZE);
	tmp1 = mat_new(DCTSIZE);
	tmp2 = mat_new(DCTSIZE);

	for (i = 0; i < DCTSIZE; i++)
		for (j = 0; j < DCTSIZE; j++)
			D[i][j] = _D[i][j];

	mat_transpose(Dt, D, DCTSIZE);

	dct_inited = 1;
}

void
idct(short *out, short *in)
{
	if (!dct_inited)
		dct_init();

	/* Convert to internal matrix representation */
	dcttomat(tmp1, in);
	/* Do D' * A * D */

	mat_mul(tmp2, Dt, tmp1, DCTSIZE);
	mat_mul(tmp1, tmp2, D, DCTSIZE);

	/* Convert back to dct representation */
	mattodct(out, tmp1);
}

void
dct(short *out, short *in)
{
	if (!dct_inited)
		dct_init();

	/* Convert to internal matrix representation */
	dcttomat(tmp1, in);
	/* Do D' * A * D */

	mat_mul(tmp2, D, tmp1, DCTSIZE);
	mat_mul(tmp1, tmp2, Dt, DCTSIZE);

	/* Convert back to dct representation */
	mattodct(out, tmp1);
}
