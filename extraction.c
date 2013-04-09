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
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <jpeglib.h>

#include "config.h"
#include "common.h"
#include "extraction.h"

struct transform {
	char *name;
	double *(*transform)(short *, int, int *);
};

double *spline_transform(short *, int, int *);
double *gradient_transform(short *, int, int *);
double *roughness_transform(short *, int, int *);
double *diffsquare_transform(short *, int, int *);

struct transform cd_transforms[] = {
	{ "spline", spline_transform },
	{ "gradient", gradient_transform },
	{ "rough", roughness_transform },
	{ "diffsquare", diffsquare_transform },
	{ NULL, NULL}
};

transform_t
transform_lookup(char *name)
{
	struct transform *tmp;

	for (tmp = &cd_transforms[0]; tmp->name != NULL; tmp++) {
		if (strcmp(name, tmp->name) == 0)
			return (tmp->transform);
	}

	return (NULL);
}

int
compute_stats(double *hist, int size,
    double *pmean, double *pstd, double *pskew, double *pkurt)
{
	double mean, std, skew, kurt;
	double tmp, tmp2;
	int i;

	mean = 0;
	for (i = 0; i < size; i++)
		mean += hist[i];
	mean /= size;

	if (size < 2) {
		*pstd = 0;
		*pmean = 0;
		*pskew = 0;
		return (-1);
	}

	std = skew = kurt = 0;
	for (i = 0; i < size; i++) {
		tmp = hist[i] - mean;
		tmp2 = tmp*tmp;
		std += tmp2;
		skew += tmp2 * tmp; 
		kurt += tmp2 * tmp2;
	}
	std = sqrt(std / (size - 1));

	if (std != 0) {
		skew = skew / (size - 1) / (std * std * std);
		kurt = kurt / (size - 1) / (std * std * std * std) - 3;
	} else {
		skew = 0;
		kurt = 0;
	}

	*pskew = skew;
	*pstd = std;
	*pmean = mean;
	*pkurt = kurt;

	return (0);
}

void
spline_setup(double *x, double *y, int n, double *y2, double *u)
{
	int i;
	double sig, p, un;

	y2[0] = -0.5;
	u[0] = (3.0/(x[1] - x[0])*((y[1] - y[0])/(x[1] - x[0])));

	for (i = 1; i < n - 1; i++) {
		sig = (x[i] - x[i-1])/(x[i+1] - x[i-1]);
		p = sig * y2[i-1] + 2.0;
		y2[i] = (sig - 1.0)/p;
		u[i] = (y[i+1] - y[i])/(x[i+1] - x[i]) - (y[i] - y[i-1])/(x[i]- x[i-1]);
		u[i] = (6.0*u[i]/(x[i+1]-x[i-1])-sig*u[i-1])/p;
	}
	un = (3.0/(x[n-1]-x[n-2])*(y[n-2]-y[n-1])/(x[n-1] - x[n-2]));
	y2[n-1] = (un - 0.5*u[n-2])/(0.5*y2[n-2] + 1.0);

	for (i = n-2; i >= 0; i--) {
		y2[i] = y2[i]*y2[i+1]+u[i];
	}
}

double
spline_inter(double *x, double *y, double *y2, int n, int lo, int hi)
{
	double h, a, b, res;

	h = 2;
	a = 0.5;
	b = 0.5;

	res = a*y[lo] + b*y[hi]+((a*a*a - a)*y2[lo] + (b*b*b-b)*y2[hi])*(h*h)/6.0;
	return (res);
}

#define SPLINEN	10

double
esterror2(double *h, int ind, int max)
{
	int i, s, e, n, lo = 0, hi = 0;
	double error;
	double x[SPLINEN], y[SPLINEN];
	double y2[SPLINEN], u[SPLINEN];

	s = ind - SPLINEN/2;
	if (s < 0)
		s = 0;
	e = ind + SPLINEN/2;
	if (e > max)
		e = max;

	n = 0;
	for (i = s; i <= e && n < SPLINEN; i++) {
		if (i == ind) {
			lo = n - 1;
			hi = n;
			continue;
		}
		x[n] = i;
		y[n] = h[i];
		n++;
	}

	if (hi >= n)
		err(1, "FUCK: %d,%d: %d >= %d", s, e, hi, n);

	spline_setup(x, y, n, y2, u);

	error = spline_inter(x, y, y2, n, lo, hi);
	error -= h[ind];

	return (abs(error));
}

double
esterror(double *h, int ind)
{
	double error1, error2, est1, est2;

	est1 = (h[ind-1] - h[ind-2])/2 + h[ind-1];
	est2 = (h[ind+1] - h[ind+2])/2 + h[ind+1];

	error1 = h[ind] - est1;
	error2 = h[ind] - est2;

	return (sqrt(error1*error1 + error2*error2));
}

void
histogram(short *data, int datsiz, double **psimple, int *pnsimple)
{
	int i, n, off;
	short min, max;
	double *hist;

	min = 32767;
	max = -32767;

	for (i = 0; i < datsiz; i++) {
		if (data[i] > max)
			max = data[i];
		if (data[i] < min)
			min = data[i];
	}

	/* Round up to a multple of 512 and make sure that both min
	 * and max fit.
	 */
	n = (max - min);
	do {
		n += 1;
		n = ((n + 511) / 512) * 512;
	} while (n/2 < -min || n/2 <= max);

	min = -n/2;
		   
	if ((hist = calloc(n, sizeof(double))) == NULL)
		err(1, "calloc");

	for (i = 0; i < datsiz; i++) {
		off = data[i] - min;
		if (off < 0 || off >= n)
			errx(1, "Bad offset: %d\n", off);
		hist[off]++;
	}

/*	for (i = 0; i < n; i++)
		fprintf(stderr, "%d %d\n", i, (int)hist[i]);
*/
	*pnsimple = n;
	*psimple = hist;
}

double
distribution(int slot, short *data, int bits, double *p)
{
	double mean, std, skew, kurt;
	int i, j, n, nsimple;
	double *simple;
	double *herror;
	short *ndata;

	n = bits / DCTSIZE2;

	if ((ndata = malloc(n * sizeof(short))) == NULL)
		err(1, "malloc");

	for (j = 0; j < n; j++)
		ndata[j] = data[j*DCTSIZE2 + slot];

	histogram(ndata, n, &simple, &nsimple);

	free(ndata);

	if ((herror = calloc(nsimple, sizeof(double))) == NULL)
		err(1, "malloc");

	for (i = 2; i < nsimple - 2; i++) {
		if (simple[i] != 0)
			herror[i] = esterror2(simple, i, nsimple-1)/simple[i];
		else
			herror[i] = 0;
		/* fprintf(stderr, "%d %f\n", i, herror[i]); */
	}

	compute_stats(herror, nsimple, &mean, &std, &skew, &kurt);

	free(simple);
	free(herror);

	*p++ = mean;
	*p++ = std;
	*p++ = skew;
	*p++ = kurt;

	return (0);
}

void
depprint(double *hm, double *hd, double *hs, double *hk)
{
	int i, n;

	double hmean[256], hstd[256], hskew[256], hkurt[256];
	double mean, std, skew, kurt;

	n = 0;
	for (i = 0; i < 256; i++) {
		hmean[n] = esterror(hm, i);
		hstd[n] = esterror(hd, i);
		hskew[n] = esterror(hs, i);
		hkurt[n] = esterror(hk, i);
		if (hm[i] != 0 && hd[i] != 0)
			n++;
	}

	compute_stats(hmean, n, &mean, &std, &skew, &kurt);
	fprintf(stdout, "%f %f %f %f ", mean, std, skew, kurt);
	compute_stats(hstd, n, &mean, &std, &skew, &kurt);
	fprintf(stdout, "%f %f %f %f ", mean, std, skew, kurt);
	compute_stats(hskew, n, &mean, &std, &skew, &kurt);
	fprintf(stdout, "%f %f %f %f ", mean, std, skew, kurt);
	compute_stats(hkurt, n, &mean, &std, &skew, &kurt);
	fprintf(stdout, "%f %f %f %f ", mean, std, skew, kurt);
}

double
dependency(int one, int two, short *data, int bits,
    double *hmean, double *hstd, double *hskew, double *hkurt)
{
	double mean, std, skew, kurt;
	int i, j, n;
	short dct, dctnext;
	u_short model[256][256];
	double *simple[256];
	size_t nsimple[256];

	memset(hmean, 0, 256*sizeof(double));
	memset(hstd, 0, 256*sizeof(double));
	memset(hskew, 0, 256*sizeof(double));
	memset(hkurt, 0, 256*sizeof(double));

	memset(model, 0, sizeof(model));
	memset(simple, 0, sizeof(simple));
	for (i = 0; i < bits; i+= DCTSIZE2) {
		dct = data[i + one];
		dctnext = data[i + two];

		dct += 128;
		dctnext += 128;

		if (dct > 255 || dctnext > 255)
			continue;

		model[dct][dctnext]++;
	}

	for (i = 0; i < 256; i++) {
		double *p;
		n = 0;
		for (j = 0; j < 256; j++) {
			n += model[i][j];
		}
		nsimple[i] = n;
		simple[i] = malloc(n * sizeof(double));
		if (simple[i] == NULL)
			err(1, "malloc");

		p = simple[i];
		for (j = 0; j < 256; j++) {
			n = model[i][j];
			while (n-- > 0)
				*p++ = j;
		}
	}

	n = 0;
	for (i = 0; i < 256; i++) {
		if (nsimple[i] > 4) {
			compute_stats(simple[i], nsimple[i],
			    &mean, &std, &skew, &kurt);
			hmean[i] = mean;
			hstd[i] = std;
			hskew[i] = skew;
			hkurt[i] = kurt;
			n++;
		}
	}

	for (i = 0; i < 256; i++)
		free(simple[i]);

	return (0);
}

/*
 * Calculates a histogram for certain DCT coefficients and then
 * uses spline interpolation to calculate an error distribution.
 */

#define HOWMANY	18

double *
spline_transform(short *dcts, int bits, int *pnpoints)
{
	static double output[HOWMANY*4];
	double *p;
	int i;

	p = output;
	for (i = 0; i < HOWMANY; i++) {
		distribution(i, dcts, bits, p);
		p += 4;
	}

	*pnpoints = HOWMANY*4;

	return (output);
}

double *
gradient_transform(short *dcts, int bits, int *pnpoints)
{
	double *output;
	short *ndcts;

	if (prepare_all_gradx(&ndcts, &bits) == -1)
		errx(1, "%s: gradx failed", __func__);
	
	output = spline_transform(ndcts, bits, pnpoints);

	free(ndcts);

	return (output);
}

double *
roughness_transform(short *dcts, int bits, int *pnpoints)
{
	double mean, std, skew, kurt;
	static double output[8];
	double *poutput, *points;
	int i, j, n, off, npoints;

	n = bits / DCTSIZE2;

	if ((points = malloc(n * sizeof(double))) == NULL)
		err(1, "malloc");

	/* Computes frequency averaged roughness */
	for (i = 0; i < n; i++) {
		double sum = 0, weight = 0, val;

		off = i * DCTSIZE2;
		for (j = 0; j < DCTSIZE2; j++) {
			int u, v;
			val = dcts[off + j];

			u = j / 8;
			v = j % 8;

			sum += (u*u + v*v) * val * val;
			weight += (u*u + v*v);
		}
		points[i] = sqrt(sum/weight);
	}

	compute_stats(points, n, &mean, &std, &skew, &kurt);

	npoints = 0;
	poutput = output;
	*poutput++ = mean;
	*poutput++ = std;
	*poutput++ = skew;
	*poutput++ = kurt;
	npoints += 4;

	*pnpoints = npoints;

	free(points);

	return (output);
}

double *
diffsquare_transform(short *dcts, int bits, int *pnpoints)
{
	double mean, std, skew, kurt;
	static double output[64];
	double *poutput, *points;
	int i, j, k, n, off, npoints;

	n = bits / DCTSIZE2;

	if ((points = malloc(n * sizeof(double))) == NULL)
		err(1, "malloc");

	npoints = 0;
	poutput = output;

	for (k = 0; k < DCTSIZE; k++) {
		for (i = 0; i < n; i++) {
			double sum = 0;
			double val1, val2, weight = 0;

			off = i * DCTSIZE2;
			for (j = 0; j < DCTSIZE - 1; j++) {
				val1 = dcts[off + k * DCTSIZE + j];
				val2 = dcts[off + k * DCTSIZE + j + 1];

				sum += (val2 - val1) * (val2 - val1);
				weight += abs(val1);
			}
			points[i] = sqrt(sum)/(weight + 1);
		}
		
		compute_stats(points, n, &mean, &std, &skew, &kurt);

		*poutput++ = mean;
		*poutput++ = std;
		*poutput++ = skew;
		*poutput++ = kurt;
		npoints += 4;
	}

	for (k = 0; k < DCTSIZE; k++) {
		for (i = 0; i < n; i++) {
			double sum = 0;
			double val1, val2, weight = 0;

			off = i * DCTSIZE2;
			for (j = 0; j < DCTSIZE - 1; j++) {
				val1 = dcts[off + k + DCTSIZE * j];
				val2 = dcts[off + k + DCTSIZE * (j + 1)];

				sum += (val2 - val1) * (val2 - val1);
				weight += abs(val1);
			}
			points[i] = sqrt(sum)/(weight + 1);
		}
		
		compute_stats(points, n, &mean, &std, &skew, &kurt);

		*poutput++ = mean;
		*poutput++ = std;
		*poutput++ = skew;
		*poutput++ = kurt;
		npoints += 4;
	}

	*pnpoints = npoints;

	free(points);

	return (output);
}
