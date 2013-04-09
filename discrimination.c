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
#include <sys/queue.h>
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
#include "discrimination.h"

struct cd_entry {
	TAILQ_ENTRY(cd_entry) next;

	char *filename;
	int positive;
	int npoints;
	double *points;
};

struct cd_decision {
	TAILQ_ENTRY(cd_decision) next;
	char *name;

	int npoints;	/* data points for each sample */

	double *b;	/* projection values */
	double k;	/* decision boundary */
	double where;	/* parameter from testing */
	double projpos;
	double projneg;

	TAILQ_HEAD(cd_list, cd_entry) positiveq;
	int npositive;

	struct cd_list negativeq;
	int nnegative;

	char *transform_name;
	transform_t transform;
};

static TAILQ_HEAD(cdqueue, cd_decision) cdq;

#define CD_PERCENT	(0.8)

void
cd_init(void)
{
	TAILQ_INIT(&cdq);
}

void
cd_insert(struct cd_decision *cdd)
{
	TAILQ_INSERT_TAIL(&cdq, cdd, next);
}

struct cd_decision *
cd_iterate(struct cd_decision *cdd)
{
	if (cdd == NULL)
		return (TAILQ_FIRST(&cdq));
	return (TAILQ_NEXT(cdd, next));
}

struct cd_decision *
cd_new(void)
{
	struct cd_decision *cdd;
	
	if ((cdd = calloc(1, sizeof(struct cd_decision))) == NULL)
		err(1, "calloc");

	TAILQ_INIT(&cdd->positiveq);
	TAILQ_INIT(&cdd->negativeq);

	return (cdd);
}

#define MAX_NPOINTS	1024

/* Read a single line of data and return an array of doubles */

double *
cd_collect_data(char *data, int *pnpoints)
{
	double points[MAX_NPOINTS], *ppoints;
	char *p, *endp;
	int n = 0;

	while (n < MAX_NPOINTS && data != NULL && *data != '\0') {
		p = strsep(&data, " ");
		if (p == NULL)
			return (NULL);
		/* Ignore multiple spaces */
		if (!strlen(p))
			continue;

		points[n] = strtod(p, &endp);
		if (endp == NULL || *endp != '\0')
			return (NULL);

		n++;
	}

	if ((ppoints = malloc(n * sizeof(double))) == NULL)
		return (NULL);

	memcpy(ppoints, points, n * sizeof(double));

	*pnpoints = n;

	return (ppoints);
}

void
cd_make_entry(struct cd_decision *cdd, char *name, int positive,
    double *points, int npoints)
{
	struct cd_entry *entry;

	if ((entry = calloc(1, sizeof(struct cd_entry))) == NULL)
		err(1, "calloc");

	if ((entry->filename = strdup(name)) == NULL)
		err(1, "strdup");
	entry->positive = positive;
	entry->points = points;
	entry->npoints = npoints;

	if (positive) {
		TAILQ_INSERT_TAIL(&cdd->positiveq, entry, next);
		cdd->npositive++;
	} else {
		TAILQ_INSERT_TAIL(&cdd->negativeq, entry, next);
		cdd->nnegative++;
	}
}

int
cd_process_file(struct cd_decision *cdd, char *filename)
{
	FILE *fin;
	char line[2048], *p;
	char *imagename, *type, *tfname, *tmp, *data;
	double *points;
	int linenr = 0, npoints = 0, tmpnpoints;

	if ((fin = fopen(filename, "r")) == NULL)
		err(1, "fopen: %s", filename);

	if (cdd->npoints)
		npoints = cdd->npoints;

	while (fgetl(line, sizeof(line), fin) != NULL) {
		linenr++;
		p = line;

		imagename = strsep(&p, ":");
		tmp = strsep(&p, ":");
		data = strsep(&p, ":");

		if (tmp == NULL || data == NULL)
			errx(1, "\"%s\": bad format in line %d",
			    filename, linenr);

		type = strsep(&tmp, ",");
		tfname = strsep(&tmp, ",");

		if (type == NULL || tfname == NULL)
			errx(1, "\"%s\": bad format in line %d",
			    filename, linenr);

		if (cdd->transform_name != NULL &&
		    strcmp(cdd->transform_name, tfname))
			errx(1, "\"%s\": transform name changed in line %d",
			    filename, linenr);
		else {
			cdd->transform = transform_lookup(tfname);
			if (cdd->transform == NULL)
				errx(1, "\"%s\": unknown transform in line %d",
				    filename, linenr);
			if ((cdd->transform_name = strdup(tfname)) == NULL)
				err(1, "strdup");
		}

		points = cd_collect_data(data, &tmpnpoints);
		if (points == NULL)
			errx(1, "\"%s\": bad data in line %d",
			    filename, linenr);
		if (npoints && tmpnpoints != npoints)
			errx(1, "\"%s\": require %d data points but got %d in line %d",
			    filename, npoints, tmpnpoints, linenr);
		if (!npoints)
			npoints = tmpnpoints;

		cd_make_entry(cdd, imagename, atoi(type), points, npoints);
	}

	fclose(fin);

	if (!cdd->npoints)
		cdd->npoints = npoints;

	return (0);
}

void
cd_meanest(struct cd_list *head, int howmany, double *mest, int mestsize)
{
	int i;
	struct cd_entry *tmp;

	for (i = 0; i < mestsize; i++) {
		int count = 0;
		double mean = 0;

		TAILQ_FOREACH(tmp, head, next) {
			mean += tmp->points[i];
			count++;

			if (howmany && count >= howmany)
				break;
		}

		mest[i] = mean / count;
	}
}

/* Compute the estimator for the covariance matrix */

void
cd_covarest(struct cd_decision *cdd, int npositive, int nnegative,
    double *mestpos, double *mestneg, double **covarest)
{
	int i, j;
	int npoints = cdd->npoints;
	struct cd_entry *tmp;

	for (i = 0; i < npoints; i++) {
		for (j = 0; j < npoints; j++) {
			double sum1, sum2;
			int count1, count2;

			sum1 = 0;
			count1 = 0;
			TAILQ_FOREACH(tmp, &cdd->positiveq, next) {
				sum1 += (tmp->points[i] - mestpos[i]) *
				    (tmp->points[j] - mestpos[j]);
				count1++;
				if (npositive && count1 >= npositive)
					break;
			}

			sum2 = 0;
			count2 = 0;
			TAILQ_FOREACH(tmp, &cdd->negativeq, next) {
				sum1 += (tmp->points[i] - mestneg[i]) *
				    (tmp->points[j] - mestneg[j]);
				count2++;
				if (nnegative && count2 >= nnegative)
					break;
			}

			covarest[i][j] = (sum1 + sum2) / (count1 + count2 - 2);
		}
	}
}

double
cd_project(double *a, double *b, int npoints)
{
	int i;
	double sum = 0;

	for (i = 0; i < npoints; i++)
		sum += *a++ * *b++;

	return (sum);
}

void
cd_dump(FILE *fout, struct cd_decision *cdd)
{
	int i;

	fprintf(fout, "%s\n", cdd->name);
	fprintf(fout, "%s\n", cdd->transform_name);
	fprintf(fout, "%d", cdd->npoints);
	for (i = 0; i < cdd->npoints; i++)
		fprintf(fout, " %f", cdd->b[i]);
	fprintf(fout, "\n");

	fprintf(fout, "%f %f %f\n", cdd->projpos, cdd->projneg, cdd->k);
}

struct cd_decision *
cd_read(FILE *fin)
{
	char line[1024], *p, *str;
	struct cd_decision *cdd;
	int n;

	if ((cdd = calloc(1, sizeof(struct cd_decision))) == NULL)
		return (NULL);

	/* Read name */
	if (fgetl(line, sizeof(line), fin) == NULL)
		goto error;
	if ((cdd->name = strdup(line)) == NULL)
		goto error;

	/* Read transform */
	if (fgetl(line, sizeof(line), fin) == NULL)
		goto error;
	if ((cdd->transform_name = strdup(line)) == NULL)
		goto error;
	if ((cdd->transform = transform_lookup(cdd->transform_name)) == NULL) {
		fprintf(stderr, "Unknown transforms \"%s\" for \"%s\"\n",
		    cdd->transform_name, cdd->name);
		goto error;
	}

	/* Read projection */
	if (fgetl(line, sizeof(line), fin) == NULL)
		goto error;

	p = line;
	str = strsep(&p, " ");
	if (str == NULL || p == NULL)
		goto error;
	cdd->npoints = atoi(str);
	cdd->b = cd_collect_data(p, &n);
	if (cdd->b == NULL || cdd->npoints != n) {
		fprintf(stderr, "Format %s: projection data malformed\n",
		    cdd->name);
		goto error;
	}

	/* Read projection */
	if (fgetl(line, sizeof(line), fin) == NULL)
		goto error;

	n = sscanf(line, "%lf %lf %lf", &cdd->projpos, &cdd->projneg, &cdd->k);
	if (n != 3) {
		fprintf(stderr, "Format %s: boundary values malformed\n",
		    cdd->name);
	}

	fprintf(stderr, "Read detection for %s\n", cdd->name);
	return (cdd);
 error:
	if (cdd->name)
		free(cdd->name);
	if (cdd->b)
		free(cdd->b);
	
	free (cdd);
	return (NULL);
}

void
cd_compute(struct cd_decision *cdd, char *name, int test)
{
	int i, j, npoints;
	double *mestpos, *mestneg, **covarest;
	int nnegative, npositive;

	if (cdd->nnegative < 2 || cdd->npositive < 2)
		errx(1, "Not enough data points for \"%s\"", name);

	/* Free memory if in use before */
	if (cdd->name) {
		free(cdd->name);
		cdd->name = NULL;
	}
	if (cdd->b) {
		free(cdd->b);
		cdd->b = NULL;
	}

	npoints = cdd->npoints;
	mestpos = malloc(npoints * sizeof(double));
	mestneg = malloc(npoints * sizeof(double));
	covarest = malloc(npoints * sizeof(double *));
	if (mestpos == NULL || mestneg == NULL || covarest == NULL)
		err(1, "malloc");

	if ((cdd->name = strdup(name)) == NULL)
		err(1, "strdup");
	cdd->b = malloc(npoints * sizeof(double *));
	if (cdd->b == NULL)
		err(1, "malloc");

	for (i = 0; i < npoints; i++) {
		covarest[i] = malloc(npoints * sizeof(double));
		if (covarest[i] == NULL)
			err(1, "malloc");
	}

	/*
	 * In the test case, we use 80% of the images to train our
	 * system, and the remaining 20% to test is accuracy.
	 * Otherwise, we want to use all images.
	 */
	if (test) {
		npositive = cdd->npositive * CD_PERCENT;
		nnegative = cdd->nnegative * CD_PERCENT;
	} else {
		npositive = cdd->npositive;
		nnegative = cdd->nnegative;
	}

	cd_meanest(&cdd->positiveq, npositive, mestpos, npoints);
	cd_meanest(&cdd->negativeq, nnegative, mestneg, npoints);

	cd_covarest(cdd, npositive, nnegative, mestpos, mestneg, covarest);

	matrix_invert(covarest, npoints);

	for (i = 0; i < npoints; i++) {
		double diff, sum = 0;
		for (j = 0; j < npoints; j++) {
			diff = mestpos[j] - mestneg[j];
			sum += covarest[i][j] * diff;
		}

		cdd->b[i] = sum;
	}

	/* Free up memory */
	for (i = 0; i < npoints; i++)
		free(covarest[i]);
	free(covarest);

	cdd->projpos = cd_project(mestpos, cdd->b, npoints);
	cdd->projneg = cd_project(mestneg, cdd->b, npoints);
	cdd->k = (cdd->projpos + cdd->projneg) / 2;

	cd_setboundary(cdd, cdd->where);

	free(mestpos);
	free(mestneg);
}

void
cd_setboundary(struct cd_decision *cdd, double where)
{
	double k;

	if (cdd->projpos > cdd->projneg)
		k = (cdd->projpos - cdd->projneg) * where + cdd->projneg;
	else
		k = (cdd->projneg - cdd->projpos) * (1 - where) + cdd->projpos;

	cdd->k = k;
}

int
cd_classify(struct cd_decision *cdd, double *points)
{
	double val;

	val = cd_project(points, cdd->b, cdd->npoints);

	if (cdd->projpos > cdd->projneg) {
		return (val > cdd->k);
	} else {
		return (val < cdd->k);
	}
}

void
cd_test(struct cd_decision *cdd)
{
	struct cd_entry *tmp;
	int negcorrect, poscorrect;
	int negfalse, posfalse;
	int nnegative, npositive;
	double where, fprate;
	int saved = 0, count, allcount;

	fprintf(stderr, "%6.4f %6.4f\n", 1.0, 1.0);
	for (where = -1; where <= 2; where += 0.15) {
		negcorrect = negfalse = 0;
		poscorrect = posfalse = 0;

		cd_setboundary(cdd, where);

		npositive = cdd->npositive * CD_PERCENT;
		nnegative = cdd->nnegative * CD_PERCENT;

		allcount = 0;
		count = 0;
		TAILQ_FOREACH(tmp, &cdd->negativeq, next) {
			count++;
			if (count <= nnegative)
				continue;

			if (!cd_classify(cdd, tmp->points))
				negcorrect++;
			else
				negfalse++;
			allcount++;
		}

		count = 0;
		TAILQ_FOREACH(tmp, &cdd->positiveq, next) {
			count++;
			if (count <= npositive)
				continue;

			if (cd_classify(cdd, tmp->points))
				poscorrect++;
			else
				posfalse++;
			allcount++;
		}

		fprate = (float)negfalse/(negfalse + negcorrect)*1.0;
		fprintf(stderr, "%6.4f %6.4f - %6.5f\n", fprate,
		    (float)poscorrect/(posfalse + poscorrect)*1.0,
		    (float)(posfalse + negfalse)/allcount);

		/* Find a false positive rate that is below 1% */
		if (!saved && fprate < 1) {
			saved = 1;
			cdd->where = where;
		}
	}

	/* Did not find a good false positive rate, use the middle */
	if (!saved)
		cdd->where = 0.5;

	fprintf(stderr, "%6.4f %6.4f\n", 0.0, 0.0);
}

char *
cd_name(struct cd_decision *cdd)
{
	return (cdd->name);
}

transform_t
cd_transform(struct cd_decision *cdd)
{
	return (cdd->transform);
}
