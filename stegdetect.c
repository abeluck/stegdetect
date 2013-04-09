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

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#include <jpeglib.h>
#include <file.h>

#include "common.h"
#include "extraction.h"
#include "discrimination.h"

#define DBG_PRINTHIST	0x0001
#define DBG_CHIDIFF	0x0002
#define DBG_CHICALC	0x0004
#define DBG_CHIEND	0x0008
#define DBG_PRINTONES	0x0010
#define DBG_CHI		0x0020
#define DBG_ENDVAL	0x0040
#define DBG_BINSRCH	0x0080
#define DBG_PRINTZERO	0x0100

#define FLAG_DOOUTGUESS	0x0001
#define FLAG_DOJPHIDE	0x0002
#define FLAG_DOJSTEG	0x0004
#define FLAG_DOINVIS	0x0008
#define FLAG_DOF5	0x0010
#define FLAG_DOF5_SLOW	0x0020
#define FLAG_DOAPPEND	0x0040
#define FLAG_DOTRANSF	0x0080
#define FLAG_DOCLASSDIS	0x0100
#define FLAG_CHECKHDRS	0x1000
#define FLAG_JPHIDESTAT	0x2000

float chi2cdf(float chi, int dgf);
double detect_f5(char *);

char *progname;

float DCThist[257];
float scale = 1;		/* Sensitivity scaling */

static int debug = 0;
static int quiet = 0;
static int ispositive = 0;	/* Current images contain stego */
static char *transformname;	/* Current transform name */

static short *olddata;
static int oldx, oldy;
static transform_t transform;

void
buildDCTreset(void)
{
	olddata = NULL;
	oldx = oldy = 0;
}

void
buildDCThist(short *data, int x, int y)
{
	int i, min, max;
	int off, count, sum;

	if (olddata != data || x < oldx || y < oldy ||
	    x - oldx + y - oldy >= y - x) {
		olddata = data;
		oldx = x;
		oldy = y;

		memset(DCThist, 0, sizeof(DCThist));
	} else {
		for (i = oldx; i < x; i++) {
			off = data[i];

			/* Don't know what to do about DC! */
			if (off < -128)
				continue;
			else if (off > 127)
				continue;

			DCThist[off + 128]--;
		}

		olddata = data;
		oldx = x;

		x = oldy;

		oldy = y;
	}

	min = 2048;
	max = -2048;

	/* Calculate coefficent frequencies */
	sum = count = 0;
	for (i = x; i < y; i++) {
		if ((i & ~63) == i) {
			if (debug & DBG_PRINTONES)
				fprintf(stdout, "%d] %d\n", i, count);
			sum += count;
			count = 0;
		}

		off = data[i];
		if (off == 1)
			count++;

		if (off < min)
			min = off;
		if (off > max)
			max = off;

		/* Don't know what to do about DC! */
		if (off < -128)
			continue;
		else if (off > 127)
			continue;
		
		DCThist[off + 128]++;
	}

	if (debug & DBG_PRINTHIST) {
		for (i = 0; i < 256; i++) {
			fprintf(stdout, "%4d: %8.1f\n", i - 128, DCThist[i]);
		}

		fprintf(stdout, "Min: %d, Max: %d, Sum-1: %d\n",
			min, max, sum);
	}
}

/*
 * Self calibration on bad test example.
 */

int
unify_false_jsteg(float *hist, float *theo, float *obs, float *discard)
{
	int i, size = 0;

	/* Build theoretical histogram */
	for (i = 0; i < 128; i++) {
		if (i == 64 || i == 65 || i == 0)
			continue;

		theo[size] = (float)(hist[2*i - 1] + hist[2*i])/2;
		obs[size++] = hist[2*i];
	}

	return (size);
}

int
unify_false_outguess(float *hist, float *theo, float *obs, float *discard)
{
	int i, size = 0;
	int one, two;

	/* Build theoretical histogram */
	for (i = 0; i < 128; i++) {
		if (i == 64 || i == 65 || i == 0)
			continue;

		one = hist[2*i - 1];
		two = hist[2*i];

		theo[size] = (float)(one + two)/2;
		obs[size++] = two;
	}

	return (size);
}


int
unify_false_jphide(float *hist, float *theo, float *obs, float *discard)
{
	int i, size = 0;

	/* Build theoretical histogram */
	for (i = 0; i < 128; i++) {
		if (i == 64)
			continue;

		if (i < 64) {
			theo[size] = (float)(hist[2*i] + hist[2*i + 1])/2;
			obs[size++] = hist[2*i + 1];
		} else {
			theo[size] = (float)(hist[2*i - 1] + hist[2*i])/2;
			obs[size++] = hist[2*i];
		}
	}

	return (size);
}

int
unify_normal(float *hist, float *theo, float *obs, float *discard)
{
	int i, size = 0;

	/* Build theoretical histogram */
	for (i = 0; i < 128; i++) {
		if (i == 64)
			continue;

		theo[size] = (float)(hist[2*i] + hist[2*i + 1])/2;
		obs[size++] = hist[2*i + 1];
	}

	return (size);
}

int
unify_outguess(float *hist, float *theo, float *obs, float *pdiscard)
{
	int i, size = 0;
	int one, two, sum, discard;
	float f, fbar;

	sum = 0;
	for (i = 0; i < 256; i++) {
		if (i == 64 || i == 65)
			continue;
		sum += hist[i];
	}

	discard = 0;
	/* Build theoretical histogram */
	for (i = 0; i < 128; i++) {
		if (i == 64)
			continue;

		one = hist[2*i];
		two = hist[2*i + 1];

		if (one > two) {
			f = one;
			fbar= two;
		} else {
			f = two;
			fbar = one;
		}

		/* Try to check if outguess could have been used here
		 * If the smaller coefficient is less than a quarter
		 * of the larger one, then outguess has probably been
		 * not used, and we included the coefficient.
		 * Otherwise check if outguess modifications could
		 * have reduced the difference significantly.
		 */
		if ((fbar > f/4) &&
		    ((f - f/3) - (fbar + f/3) > 0)) {
			if ((debug & DBG_CHIDIFF) && (one || two))
				fprintf(stdout,
					"%4d: %8.3f - %8.3f skipped (%f)\n",
					i*2 - 128,
					(float)two,
					(float)(one + two)/2,
					(float)(one + two)/sum);
				
			discard += one + two;
			continue;
		}

		theo[size] = (float)(one + two)/2;
		obs[size++] = two;
	}

	*pdiscard = (float)discard/sum;
	return (size);
}


int
unify_jphide(float *hist, float *theo, float *obs, float *discard)
{
	int i, size;

	size = 0;
	/* First pass */
	for (i = 0; i < 256; i++) {
		/* Exclude special cases */
		if (i >= (-1 + 128) && i <= (1 + 128))
			continue;

		/* Lower bit = 0 */
		if (i < 128 && !(i & 1))
			continue;
		else if ((i >= 128) && (i & 1))
			continue;

		theo[size] = (hist[i] + hist[i + 1])/2;
		obs[size++] = hist[i];
	}

	/* Special case for 1 and -1 */
	/*
	theo[size] = (hist[-1 + 128] + hist[1 + 128] + hist[0 + 128])/2;
	obs[size++] = hist[-1 + 128] + hist[1 + 128];
	*/
	return (size);
}


float
chi2(float *DCTtheo, float *DCTobs, int size, float discard)
{
	int i, dgf;
	float chi, sumchi, ymt, ytt, f;

	ymt = ytt = 0;
	sumchi = 0;
	dgf = 0;
	for (i = 0; i < size; i++) {
		ymt += DCTobs[i];
		ytt += DCTtheo[i];

		if (debug & DBG_CHIDIFF) {
			if (DCTobs[i] || DCTtheo[i])
				fprintf(stdout, "%4d: %8.3f - %8.3f\n", i,
					DCTobs[i],
					DCTtheo[i]);
		}

		if (ytt >= 5) {
			/* Calculate chi^2 */
			chi = ymt - ytt;


			if (debug & DBG_CHICALC) {
				fprintf(stdout,
					"     (%8.3f - %8.3f)^2 = %8.3f / %8.3f = %8.3f | %8.3f\n",
					ymt, ytt,
					chi*chi, ytt, chi*chi/ytt, sumchi);
			}


			chi = chi*chi;
			chi /= ytt;

			sumchi += chi;

			dgf++;
			ymt = ytt = 0;
		}
	}

	f = 1 - chi2cdf(sumchi, dgf - 1);

	if (debug & DBG_CHIEND) {
		fprintf(stdout,
			"Categories: %d, Chi: %f, Q: %f, dis: %f -> %f\n",
			dgf, sumchi, f, discard, f * (1 - discard));
	}

	return (f * (1 - discard));
}

float
chi2test(short *data, int bits,
	 int (*unify)(float *, float *, float *, float *),
	 int a, int b)
{
	float DCTtheo[128], DCTobs[128], discard;
	int size;

	if (a < 0)
		a = 0;
	if (b > bits)
		b = bits;

	if (a >= b)
		return (-1);

	buildDCThist(data, a, b);

	discard = 0;
	size = (*unify)(DCThist, DCTtheo, DCTobs, &discard);

	return (chi2(DCTtheo, DCTobs, size, discard));
}

#define BINSEARCHVAR \
	float _max, _min, _good; \
	int _iteration

#define BINSEARCH(imin, imax, imaxiter) \
	percent = (imax); \
	_good = (imax) + 1; \
	_iteration = 0; \
	_min = (imin); \
	_max = (imax); \
	buildDCTreset(); \
	while (_iteration < (imaxiter))

#define BINSEARCH_NEXT(thresh) \
	if (debug & DBG_BINSRCH) \
		fprintf(stdout, "sum: %f, percent: %f,  good: %f\n", \
			sum, percent, _good); \
	if (_iteration == 0) { \
		if (sum >= (thresh)) \
			break; \
		_good = percent; \
		percent = _min; \
	} else \
	if (sum < (thresh)) { \
		_good = percent; \
		if (_good == _min) /* XXX */\
			break; /* XXX */\
		_max = percent; \
		percent = (_max - _min)/2 + _min; \
	} else { \
		_min = percent; \
		percent = (_max - _min)/2 + _min; \
	} \
	_iteration++

#define BINSEARCH_IFABORT(thresh) \
	percent = _good; \
	if (_good > (thresh))

int
histogram_chi_jsteg(short *data, int bits)
{
	int length, minlen, maxlen, end;
	float f, sum, percent, i, count, where;
	float max, aftercount, scale, fs;
	BINSEARCHVAR;

	if (bits == 0)
		goto abort;
	end = bits/100;
	if (end < 4000)
		end = 4000;
	BINSEARCH(200, end, 6) {
		sum = 0;
		for (i = percent; i <= bits; i += percent) {
			f = chi2test(data, bits, unify_false_jsteg, 0, i);
			if (f == 0)
				break;
			if (f > 0.4)
				sum += f * percent;
			if ((debug & DBG_CHI) && f != 0)
				fprintf(stdout, "%04f[:] %8.5f%% %f\n",
					i, f * 100, sum);
		}

		BINSEARCH_NEXT(400);
	}

	BINSEARCH_IFABORT(end) {
	abort:
		if (debug & DBG_ENDVAL)
			fprintf(stdout,
				"Accumulation: no detection possible\n");
		return (-1);
	}

	where = count = 0;
	aftercount = max = 0;
	scale = 0.95;
	sum = 0;
	for (i = percent; i <= bits; i += percent) {
		f = chi2test(data, bits, unify_normal, 0, i);
		if (f == 0)
			break;
		if (f > 0.4) {
			sum += f * percent;
			count++;
		}
		if (f >= (max * scale)) {
			if (f > max) {
				max = f;
				/* More latitude for high values */
				fs = (max - 0.4) / 0.6;
				if (fs > 0)
					scale = 1 - (0.15*fs + (1 - fs)*0.05);
			}
			aftercount = -3;
			where = i;
		} else if (f > 0.05 * max) {
			if (aftercount >= 0)
				aftercount += f * percent;
			else {
				aftercount++;
				where = i;
			}
		}

		if ((debug & DBG_CHI) &&
		    ((debug & DBG_PRINTZERO) || f != 0))
			fprintf(stdout, "%04f: %8.5f%%\n",
				i, f * 100);
	}

	length = jsteg_size(data, bits, NULL);
	minlen = where/8;
	maxlen = (where + percent)/8;
	if (debug & DBG_ENDVAL) {
		fprintf(stdout,
		    "Accumulation (%d): %f%% - %f (%f) (%d:%d - %d)\n",
		    (int)percent,
		    sum/percent, aftercount, count,
		    length, minlen, maxlen);
	}

	if (aftercount > 0)
		sum -= aftercount;
	/* Require a positive sum and at least two working samples */
	if (sum < 0 || count < 3)
		sum = 0;
	if (length < minlen/2 || length > maxlen*2)
		sum = 0;

	return (scale * sum / (2 * percent));
}

float norm_outguess[21] = {
	0.5,
	0.7071067811865475244,
	1,
	1.2247448713915890491,
	1.4142135623730950488,
	1.581138830084189666,
	1.73205080756887729353,
	1.87082869338697069279,
	2,
	2.1213203435596425732,
	2.23606797749978969641,
	2.34520787991171477728,
	2.4494897427831780982,
	2.54950975679639241501,
	2.6457513110645905905,
	2.73861278752583056728,
	2.8284271247461900976,
	2.91547594742265023544,
	3,
	3.08220700148448822513,
	3.162277660168379332
};

int
histogram_chi_outguess(short *data, int bits)
{
	int i, off, range;
	float percent, count;
	float f, sum, norm;
	BINSEARCHVAR;

	BINSEARCH(0.1, 10, 9) {
		range = percent*bits/100;
		sum = 0;
		for (i = 0; i <= 100; i ++) {
			off = i*bits/100;
			f = chi2test(data, bits, unify_false_outguess,
				     off - range, off + range);
			sum += f;
			if ((debug & DBG_CHI) && f != 0)
				fprintf(stdout, "%04d[:] %8.5f%%\n",
					i, f * 100);
		}

		BINSEARCH_NEXT(0.6);
	}

	/* XXX */
	BINSEARCH_IFABORT(10)
		return (0);
	range = percent*bits/100;
	count = 0;
	sum = 0;
	for (i = 0; i <= 100; i ++) {
		off = i*bits/100;
		f = chi2test(data, bits, unify_outguess,
			     off - range, off + range);
		if (f > 0.25)
			sum += f;
		if (f > 0.001)
			count++;
		if ((debug & DBG_CHI) && 
		    ((debug & DBG_PRINTZERO) || f != 0))
			fprintf(stdout, "%04d: %8.5f%%\n", i, f * 100);
	}

	count /= percent;

	off = percent * 2;
	if (off >= sizeof(norm_outguess)/sizeof(float))
		off = sizeof(norm_outguess)/sizeof(float) - 1;

	norm = sum / norm_outguess[off];

	if (debug & DBG_ENDVAL)
		fprintf(stdout,
			"Accumulation (%4.1f%%): %8.3f%% (%8.3f%%) (%4.1f)\n",
			percent,
			sum * 100,
			norm * 100,
			count);

	/* XXX - some wild adjustment */
	if (count < 15) {
		sum -= (15 - count) * 0.5;
		if (sum < 0)
			sum = 0;
	}

	return (scale * sum / 0.5);
}

int
jphide_runlength(short *data, int bits)
{
	int i, max = -1;
	short coeff, rundct[128], runmdct[128];
	int runlen[128], runmlen[128], off;

	memset(rundct, 0, sizeof(rundct));
	memset(runlen, 0, sizeof(runlen));
	memset(runmdct, 0, sizeof(runmdct));
	memset(runmlen, 0, sizeof(runmlen));

	for (i = 0; i < bits; i++) {
		coeff = data[i];

		if (coeff < -127 || coeff > 127)
			continue;

		if (coeff >= -1 && coeff <= 1)
			continue;

		if (coeff < 0)
			off = -coeff/2;
		else
			off = coeff/2 + 64;

		if (rundct[off] != coeff) {
			if (runlen[off] > 1 && runlen[off] > runmlen[off]) {
				runmlen[off] = runlen[off];
				runmdct[off] = rundct[off];
			}
			rundct[off] = coeff;
			runlen[off] = 1;
		} else {
			runlen[off]++;

			if (runlen[off] > max)
				max = runlen[off];
		}
	}

	return (max);
}

int
jphide_zero_one(void)
{
	int one, zero, res, sum;
	int negative = 0;

	/* Zero and One have a 1/4 chance to be modified, back project */
	one = DCThist[-1 + 128] + DCThist[1 + 128];
	zero = DCThist[0 + 128];
	sum = one + zero;
	if (sum > 10) {
		if (one > zero)
			res = (3*zero - one);
		else
			res = (3*one - zero);

		if (res < -1)
			negative = 1;
		else if (sum >= 15 && res <= -1)
			negative = 1;

		if (debug & DBG_ENDVAL)
			printf("Zero/One: %d : %d -> %5.1f%s\n",
			    one, zero, (float)res/2, negative ? " **" : "");

	}
	return (negative);
}

int
jphide_empty_pair(void)
{
	int i, res;

	res = 0;
	for (i = 0; i < 256; i++) {
		if (i >= (-1 + 128) && i <= (1 + 128))
			continue;
		if (i < 128 && !(i & 1))
			continue;
		else if (i >= 128 && (i & 1))
			continue;

		if ((DCThist[i] + DCThist[i+1]) >= 5 &&
		    (!DCThist[i] || !DCThist[i+1]))
			res++;
	}
	if (debug & DBG_ENDVAL)
		printf("Empty pairs: %d\n", res);
	if (res > 3)
		return (1);

	return (0);
}
/*
 * Calculate liklihood of JPHide embedding.
 * Pos is the last bit position where we are guaranteed to have
 * a 0.5 modification chance.
 */

int stat_runlength = 0;
int stat_zero_one = 0;
int stat_empty_pair = 0;

int
histogram_chi_jphide(short *data, int bits)
{
	int i, range, highpeak, negative;
	extern int jphpos[];
	float f, f2, sum, false;

	/* Image is too small */
	if (jphpos[0] < 500)
		return (0);

	buildDCTreset();
	f = chi2test(data, bits, unify_jphide, 0, jphpos[0]);
	if (debug & DBG_ENDVAL)
		fprintf(stdout, "Pos[0]: %04d: %8.5f%%\n", jphpos[0], f*100);

	/* If JPhide was used, we should get a high value at this position */
	if (f < 0.9)
		return (0);

	if (jphide_runlength(data, jphpos[0]) > 16) {
		stat_runlength++;
		return (0);
	}
	if (jphide_zero_one()) {
		stat_zero_one++;
		return (0);
	}

	if (jphide_empty_pair()) {
		stat_empty_pair++;
		return (0);
	}

	false = 0;
	f2 = chi2test(data, bits, unify_false_jphide, 0, jphpos[0]);
	if (debug & DBG_ENDVAL)
		fprintf(stdout, "Pos[0]: %04d[:] %8.5f%%: %8.5f%%\n",
		    jphpos[0], f2*100, (f2 - f)*100);

	/* JPHide embedding reduces f2 and increases f */
	if (f2 * 0.95 > f)
		return (0);

	f = chi2test(data, bits, unify_jphide, jphpos[0]/2, jphpos[0]);
	if (debug & DBG_ENDVAL)
		fprintf(stdout, "Pos[0]/2: %04d: %8.5f%%\n", jphpos[0], f*100);
	if (f < 0.9)
		return (0);

	f2 = chi2test(data, bits, unify_false_jphide, jphpos[0]/2, jphpos[0]);
	if (debug & DBG_ENDVAL)
		fprintf(stdout, "Pos[0]/2: %04d[:] %8.5f%%: %8.5f%%\n",
		    jphpos[0], f2*100, (f2 - f)*100);
	if (f2 * 0.95 > f)
		return (0);

	f = chi2test(data, bits, unify_jphide, 0, jphpos[0]/2);
	f2 = chi2test(data, bits, unify_false_jphide, 0, jphpos[0]/2);
	if (debug & DBG_ENDVAL)
		fprintf(stdout, "0->1/2: %04d[:] %8.5f%% %8.5f%%\n",
		    jphpos[0], f*100, f2*100);

	if (f2 * 0.95 > f)
		return (0);

	range = jphpos[0]/12;
	for (i = 11; i >= 1 && range < 250; i--)
		range = jphpos[0]/i;
	if (range < 250)
		range = 250;

	negative = highpeak = 0;
	false = sum = 0;
	for (i = range; i <= bits && (!negative || i < 4*jphpos[0]);
	    i += range) {
		f = chi2test(data, bits, unify_jphide, 0, i);
		f2 = chi2test(data, bits, unify_false_jphide, 0, i);
		
		if (i <= jphpos[0] && jphide_zero_one()) {
			stat_zero_one++;
			negative++;
		}
		if (i <= jphpos[0] && jphide_empty_pair()) {
			stat_empty_pair++;
			negative++;
		}
		if (i <= jphpos[1] && f2 >= 0.95) {
			false += f2 * range;
			if (false * 1.10 >= jphpos[1])
				negative++;
		}

		/* Special tests */
		if (f >= 0.95)
			highpeak = 1;
		if (i > jphpos[0] && !highpeak)
			negative++;
		if (highpeak && f < 0.90 && sum < jphpos[0])
			negative++;
		if (i <= jphpos[1] && f2*0.99 > f)
			negative++;
		if (f >= 0.9)
			sum += f * range;
		else if (f < 0.2)
			break;

		if ((debug & DBG_CHI) &&
		    ((debug & DBG_PRINTZERO) || f != 0))
			fprintf(stdout, "%04d: %8.5f%% %8.5f%% %.2f %.2f %s\n",
			    i, f * 100, f2*100, sum, false,
			    (i <= jphpos[0] && f2*0.99 > f) ||
			    (i <= jphpos[1] && false * 1.10 >= jphpos[1]) 
			    ? "**" : "");

	}

	sum /= 1000;

	if (debug & DBG_ENDVAL)
		fprintf(stdout, "Accumulation (neg = %d, %d): %f%% [%d]\n",
		    negative, range, sum * 100, jphpos[1]);

	if (negative)
		return (0);

	sum *= (float)1100/jphpos[0];

	return (scale * sum );
}

int
histogram_chi_jphide_old(short *data, int bits)
{
	int i, highpeak, range;
	extern int jphpos[];
	float f, sum, percent;
	int start, end;
	BINSEARCHVAR;

	end = bits/10;
	start = jphpos[0]/2;

	if (start > end)
		return (0);

	BINSEARCH(start, end, 7) {
		range = percent;
		sum = 0;
		for (i = 0; i <= bits; i += range) {
			f = chi2test(data, bits, unify_false_jphide,
				     0, i + range);
			if (f > 0.3)
				sum += f;
			else if (f < 0.2)
				break;
			if ((debug & DBG_CHI) && f != 0)
				fprintf(stdout, "%04d[:] %8.5f%%\n",
					i, f * 100);
		}

		BINSEARCH_NEXT(3);
	}

	BINSEARCH_IFABORT(end)
		return (0);

	range = percent;
	highpeak = sum = 0;
	for (i = 0; i <= bits; i += range) {
		f = chi2test(data, bits, unify_jphide,
			     0, i + range);
		if (!highpeak && f > 0.9)
			highpeak = 1;
		if (highpeak && f < 0.75)
			break;

		if (f > 0.3)
			sum += f;
		else if (f < 0.2)
			break;

		if ((debug & DBG_CHI) &&
		    ((debug & DBG_PRINTZERO) || f != 0))
			fprintf(stdout, "%04d: %8.5f%%\n", i, f * 100);
	}

	if (debug & DBG_ENDVAL)
		fprintf(stdout, "Accumulation (%4.0f): %f%%\n",
			percent,
			sum * 100);

	return (scale * sum / 7);
}

float
histogram_f5(float *hist, int size)
{
	int i, n;
	float DCTtheo[8], DCTobs[8];
	float mean;

	if (size < 63)
		return (0);

	n = 4;
	for (i = 0; i < n; i++) {
		DCTobs[i] = hist[i*8];
		mean = hist[i*8 + 1];
		if (i != 0) {
			float tmp;
			tmp = hist[i*8 - 1];
			mean = (mean + tmp)/2;
		}
		DCTtheo[i] = mean;
	}

	return (chi2(DCTtheo, DCTobs, n, 0));
}

char detect_buffer[4096];
size_t detect_buflen;

/* Copy data into buffer */

#define DETECT_MINAPPEND	128

void
detect_append(void *arg)
{
	j_decompress_ptr dinfo = arg;

	u_char *buf = (u_char *)dinfo->src->next_input_byte;
	size_t buflen = dinfo->src->bytes_in_buffer;

	if (buflen > sizeof(detect_buffer))
		buflen = sizeof(detect_buffer);

	memcpy(detect_buffer, buf, buflen);
	detect_buflen = buflen;

	if (buflen < DETECT_MINAPPEND) {
		int len;

		dinfo->src->fill_input_buffer(dinfo);
		len = dinfo->src->bytes_in_buffer;
		if (len <= 2)
			goto out;

		if (len >= sizeof(detect_buffer) - detect_buflen)
			len = sizeof(detect_buffer) - detect_buflen;
		memcpy(detect_buffer, dinfo->src->next_input_byte, len);
		detect_buflen += len;
	}

 out:
	if (detect_buflen < 4)
		detect_buflen = 0;
}

void
detect_print(void)
{
	int i;
	extern int noprint;
	u_char *buf = detect_buffer;
	size_t buflen = detect_buflen;
	char *what = "appended";

	if (buflen > 2 + 16 + 4) {
		for (i = 2; i < 2 + 16 + 4; i++)
			if (buf[i]) {
				i = 0;
				break;
			}
		if (i == 0 && !memcmp(buf + 2, buf + 18, 4)) {
			what = "camouflage";
			goto done;
		}
	}

	if (buflen > 4) {
		u_char compare[] = {0x80, 0x3f, 0xe0, 0x50};
		if (!memcmp(buf, compare, 4)) {
			what = "alpha-channel";
			goto done;
		}
	}
 done:
	fprintf(stdout, " %s(%d)<[%s][", what,
	    buflen, is_random(buf, buflen) ? "random" : "nonrandom");
	noprint = 0;
	/* Prints to stdout */
	file_process(buf, buflen);
	noprint = 1;
	fprintf(stdout, "][");
	for (i = 0; i < 16 && i < buflen; i++)
		fprintf(stdout, "%c",
		    isprint(buf[i]) ? buf[i] : '.');
	fprintf(stdout, "]> ");
}

void
class_discrimination(char *filename, int positive)
{
	double *points;
	short *dcts = NULL;
	int bits, i, npoints;

	if (prepare_all(&dcts, &bits) == -1)
		err(1, "prepare_all");

	points = transform(dcts, bits, &npoints);

	fprintf(stdout, "%s:%d,%s: ", filename, positive, transformname);

	for (i = 0; i < npoints; i++) {
		fprintf(stdout, "%.8f ", points[i]);
	}

	fprintf(stdout, "\n");

	free(dcts);
}

void
usage(void)
{
	fprintf(stderr,
	    "Usage: %s [-nqV] [-s <float>] [-d <num>] [-t <tests>] [-C <num>]\n"
	    "\t [file.jpg ...]\n",
		progname);
}

char *
quality(char *prepend, int q)
{
	static char quality[1024];
	char stars[4];
	int i;

	for (i = 0; i < q && i < 3; i++)
		stars[i] = '*';
	stars[i] = 0;

	snprintf(quality, sizeof(quality), "%s(%s)", prepend, stars);

	return (quality);
}

void
dohistogram(char *filename)
{
	short *dcts = NULL;
	int bits;

	if (jpg_open(filename) == -1)
		return;

	fprintf(stdout, "%s ->\n", filename);

	if (prepare_all(&dcts, &bits) == -1)
		goto end;

	buildDCThist(dcts, 0, bits);

	free(dcts);


 end:
	jpg_finish();
	jpg_destroy();
}

void
detect(char *filename, int scans)
{
	extern u_char *comments[];
	extern size_t commentsize[];
	extern int ncomments;
	char outbuf[1024];
	int bits, jbits;
	int res, flag;
	short *jdcts = NULL;
	short *dcts = NULL;
	int a_wasted_var;

	if (scans & FLAG_DOJSTEG) {
		prepare_jsteg(&jdcts, &jbits);
	}
	
	if (scans & FLAG_DOAPPEND) {
		detect_buflen = 0;
		stego_set_eoi_callback(detect_append);
	}

	if (jpg_open(filename) == -1) {
		if (jdcts != NULL)
			free(jdcts);
		return;
	}

	if (scans & FLAG_DOTRANSF) {
		class_discrimination(filename, ispositive);
		goto end;
	}

	if (scans & FLAG_DOAPPEND)
		stego_set_eoi_callback(NULL);

	if (scans & FLAG_DOJSTEG) {
		stego_set_callback(NULL, ORDER_MCU);
	}
	
	flag = 0;
	sprintf(outbuf, "%s :", filename);

	if (scans & FLAG_DOAPPEND) {
		if (detect_buflen)
			flag = 1;
	}

	if (scans & FLAG_DOCLASSDIS) {
		struct cd_decision *cdd;
		double *points;
		int npoints;

		if (prepare_all(&dcts, &bits) == -1)
			err(1, "prepare_all");

		for (cdd = cd_iterate(NULL); cdd; cdd = cd_iterate(cdd)) {
			transform_t transform = cd_transform(cdd);
			points = transform(dcts, bits, &npoints);
			res = cd_classify(cdd, points);

			if (!res)
				continue;

			flag = 1;
			strlcat(outbuf, " ", sizeof(outbuf));
			strlcat(outbuf, cd_name(cdd), sizeof(outbuf));
			strlcat(outbuf, "(**)", sizeof(outbuf));
		}

		free(dcts);
	}

	if (scans & FLAG_DOF5) {
		if (ncomments == 1 && commentsize[0] == 63 &&
		    !strcmp(comments[0], "JPEG Encoder Copyright 1998, James R. Weeks and BioElectroMech.")) {
			flag = 1;
			strlcat(outbuf, " f5(***)", sizeof(outbuf));
		} else if (scans & FLAG_DOF5_SLOW) {
			double beta = detect_f5(filename);
			char tmp[80];
			int stars;

			if (beta < 0.25)
				goto no_f5;

			stars = 1;
			if (beta > 0.25)
				stars++;
			if (beta > 0.4)
				stars++;

			snprintf(tmp, sizeof(tmp), " f5[%f]", beta);
			strlcat(outbuf, quality(tmp, stars), sizeof(outbuf));
			flag = 1;
		}
	no_f5:
	a_wasted_var = 0;
	}

	if (scans & FLAG_DOINVIS) {
		u_char *p;
		u_int32_t ol, length;
		int i, match = 0;

		if (ncomments < 2 || commentsize[1] < 4)
			goto no_invisiblesecrets;
		
		p = comments[1];
		length = p[3] << 24;
		length |= p[2] << 16;
		length |= p[1] << 8;
		length |= p[0];
		ol = length;
		length += 4;
		if (commentsize[1] == length)
			match = 1;

		if (!match) {
			for (i = 1; i < ncomments && length; i++) {
				if (commentsize[i] > length)
					break;
				length -= commentsize[i];
			}
			if (!length)
				match = 1;
		}

		if (match) {
			char tmp[128];

			flag = 1;
			snprintf(tmp, sizeof(tmp), " invisible[%d](***)", ol);
			strlcat(outbuf, tmp, sizeof(outbuf));
		}
		
	no_invisiblesecrets:
	a_wasted_var = 0;
	}

	if ((scans & FLAG_CHECKHDRS)) {
		/* Disable all checks if comments are present */
		if (ncomments) {
			if (jdcts != NULL)
				free(jdcts);
			scans = 0;
			if (debug & DBG_ENDVAL)
				fprintf(stdout,
				    "Disabled by comment check: %d\n",
				    ncomments);
		} else {
			int major, minor;
			u_int16_t marker;

			jpg_version(&major, &minor, &marker);
			/* Disable all checks if APP markers are present */
			if (marker) {
				if (jdcts != NULL)
					free(jdcts);
				scans = 0;
				if (debug & DBG_ENDVAL)
					fprintf(stdout,
					    "Disabled by header check: %d.%d %#0x\n",
					    major, minor, marker);
			} else if (major != 1 || minor != 1)
				/* OutGuess uses its own version of jpeg */
				scans &= ~FLAG_DOOUTGUESS;
		}
	}
	
	if (scans & FLAG_DOJSTEG) {
		/* Set via the callback */
		dcts = jdcts;
		bits = jbits;

		if (dcts == NULL)
			goto jsteg_error;
		
		res = histogram_chi_jsteg(dcts, bits);
		if (res > 0) {
			strlcat(outbuf, quality(" jsteg", res),
				sizeof(outbuf));
			flag = 1;

			/* If this detects positivly so will outguess|jphide */
			scans &= ~(FLAG_DOOUTGUESS|FLAG_DOJPHIDE);
		}

		/* Special case to disable other methods for images, that
		 * will likelty to be false positive
		 */

		if (res == -1) {
			strlcat(outbuf, " skipped (false positive likely)",
				sizeof(outbuf));
			if (!flag)
				flag = -1;
			scans &= ~(FLAG_DOOUTGUESS|FLAG_DOJPHIDE);
		}

		free(dcts);
	jsteg_error:
	a_wasted_var = 0;
	}

	if ((scans & FLAG_DOOUTGUESS) && prepare_normal(&dcts, &bits) != -1) {
		short *ndcts;
		int i, j, n, off, step;

		ndcts = malloc(bits * sizeof(short));
		if (ndcts == NULL)
			err(1, "malloc");

		step = sqrt(bits);
		n = 1;
		while (n < 2 /* step */) {
			off = 0;
			if (n > 1) {
				for (i = 0; i < n; i++) {
					for (j = i; j < bits; j += n) {
						ndcts[off++] = dcts[j];
					}
				}
			} else
				memcpy(ndcts, dcts, bits * sizeof(short));
			res = histogram_chi_outguess(ndcts, bits);
			if (res) {
				strlcat(outbuf, quality(n == 1 ?
					    " outguess(old)" : " outguess",
					    res),
				    sizeof(outbuf));
				flag = 1;
				break;
			}
			n *= 2;
		}
		free(ndcts);
		free(dcts);
	}

	if ((scans & FLAG_DOJPHIDE) && prepare_jphide(&dcts, &bits) != -1) {
		res = histogram_chi_jphide(dcts, bits);
		if (!res)
			res = histogram_chi_jphide_old(dcts, bits);
		if (res) {
			strlcat(outbuf, quality(" jphide", res),
				sizeof(outbuf));
			flag = 1;
		}
		free(dcts);
	}

	if (!flag)
		strlcat(outbuf, " negative", sizeof(outbuf));

	if (flag > 0 || !quiet) {
		fprintf(stdout, "%s", outbuf);
		if ((scans && FLAG_DOAPPEND) && detect_buflen)
			detect_print();
		fprintf(stdout, "\n");
	}
 end:
	jpg_finish();
	jpg_destroy();
}

int
main(int argc, char *argv[])
{
	int i, scans, checkhdr = 0, usecd = 0, histonly = 0;
	struct cd_decision *cdd = NULL;
	FILE *fin;
	extern char *optarg;
	extern int optind;
	int ch;

	progname = argv[0];

	scans = FLAG_DOOUTGUESS | FLAG_DOJPHIDE | FLAG_DOJSTEG | FLAG_DOINVIS |
	    FLAG_DOF5 | FLAG_DOAPPEND;

	cd_init();

	/* read command line arguments */
	while ((ch = getopt(argc, argv, "C:D:c:nhs:Vd:t:q")) != -1)
		switch((char)ch) {
		case 'h':
			histonly = 1;
			break;
		case 'c':
			if (cdd == NULL)
				cdd = cd_new();
			cd_process_file(cdd, optarg);
			usecd = 1;
			break;
		case 'D':
			if ((fin = fopen(optarg, "r")) == NULL)
				err(1, "fopen: %s", optarg);
			cdd = cd_read(fin);
			if (cdd == NULL)
				errx(1, "Invalid detection file");
			fclose(fin);
			cd_insert(cdd);
			break;
		case 'C': {
			char *strnum, *strtrans, *p;
			
			p = optarg;
			strnum = strsep(&p, ",");
			strtrans = strsep(&p, ",");

			if (strnum == NULL || strtrans == NULL ||
			    !isdigit(*optarg)) {
				usage();
				exit(1);
			}

			if ((transform = transform_lookup(strtrans)) == NULL) {
				fprintf(stderr, "Unknown transform \"%s\"\n",
				    strtrans);
				usage();
				exit(1);
			}
			
			scans = FLAG_DOTRANSF;
			
			ispositive = atoi(optarg);
			if ((transformname = strdup(strtrans)) == NULL)
				err(1, "strdup");
			break;
		}
		case 'n':
			checkhdr = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 's':
			if ((scale = atof(optarg)) == 0) {
				usage();
				exit(1);
			}
			break;
		case 'V':
			fprintf(stdout, "Stegdetect Version %s\n", VERSION);
			exit(1);
		case 'd':
			debug = atoi(optarg);
			break;
		case 't':
			scans = 0;
			for (i = 0; i < strlen(optarg); i++)
				switch(optarg[i]) {
				case 'o':
					scans |= FLAG_DOOUTGUESS;
					break;
				case 'j':
					scans |= FLAG_DOJSTEG;
					break;
				case 'p':
					scans |= FLAG_DOJPHIDE;
					break;
				case 'i':
					scans |= FLAG_DOINVIS;
					break;
				case 'f':
					scans |= FLAG_DOF5;
					break;
				case 'F':
					scans |= FLAG_DOF5 | FLAG_DOF5_SLOW;
					break;
				case 'a':
					scans |= FLAG_DOAPPEND;
					break;
				default:
					usage();
					exit(1);
				}
			break;
		default:
			usage();
			exit(1);
		}
	
	/* Set up magic rules */
	if (file_init())
		errx(1, "file magic initializiation failed");

	if (checkhdr)
		scans |= FLAG_CHECKHDRS;

	argc -= optind;
	argv += optind;

	if (usecd) {
		char *name;

		if (argc > 0)
			name = argv[0];
		else
			name = "<unknown program>";

		cd_compute(cdd, name, 1);
		cd_test(cdd);

		cd_compute(cdd, name, 0);
		cd_dump(stdout, cdd);
		exit(0);
	}

	if (cd_iterate(NULL) != NULL)
		scans |= FLAG_DOCLASSDIS;

	/* Adjust sensitivity */
	for (cdd = cd_iterate(NULL); cdd; cdd = cd_iterate(cdd)) {
		double where;

		if (scale < 0)
			where = 0;
		else if (scale < 1)
			where = scale / 2;
		else
			where = 1 - 1 / (2 * scale);

		cd_setboundary(cdd, 1 - where);
	}

	setvbuf(stdout, NULL, _IOLBF, 0);

	if (argc > 0) {
		while (argc) {
			if (histonly)
				dohistogram(argv[0]);
			else
				detect(argv[0], scans);
			
			argc--;
			argv++;
		}
	} else {
		char line[1024];

		while (fgetl(line, sizeof(line), stdin) != NULL)
			if (histonly)
				dohistogram(line);
			else
				detect(line, scans);
	}

	if (debug & FLAG_JPHIDESTAT) {
		fprintf(stdout, "Positive rejected because of\n"
		    "\tRunlength: %d\n"
		    "\tZero-One: %d\n"
		    "\tEmpty Pair: %d\n",
		    stat_runlength, stat_zero_one, stat_empty_pair);
	}

	exit(0);
}
