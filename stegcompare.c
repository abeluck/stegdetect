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
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <signal.h>

#include <jpeglib.h>

#include "config.h"
#include "common.h"

#define VERSION "0.1"

char *progname;

#define FLAG_DOOUTGUESS	0x0001
#define FLAG_DOJPHIDE	0x0002
#define FLAG_HIST	0x1000

int scans = FLAG_DOJPHIDE;

void
usage(void)
{
	fprintf(stderr,	"Usage: %s orig.jpg modified.jpg\n", progname);
}

int hist[257];
int list[257];

int
compare(const void *a, const void *b)
{
	int ia, ib;

	ia = *(int *)a;
	ib = *(int *)b;

	return (hist[ib] - hist[ia]);
}

void
docompare(char *file1, char *file2)
{
	int i;
	int bits1, bits2, count, last, sumlast;
	int hist1[257], hist2[257];
	int shist[257], shist1[257], shist2[257];
	float ratio;
	int one;
	short *dcts1, *dcts2;

	dcts1 = dcts2 = NULL;

	/* Open first file */
	if (jpg_open(file1) == -1)
		goto out;

	if (scans & FLAG_DOJPHIDE)
		prepare_jphide(&dcts1, &bits1);
	else
		prepare_all(&dcts1, &bits1);

	jpg_finish();
	jpg_destroy();

	/* Open second file */
	if (jpg_open(file2) == -1)
		goto out;

	if (scans & FLAG_DOJPHIDE)
		prepare_jphide(&dcts2, &bits2);
	else
		prepare_all(&dcts2, &bits2);

	jpg_finish();
	jpg_destroy();

	if (bits1 != bits2) {
		warnx("Size of images differs: %d != %d", bits1, bits2);
		goto out;
	}
	fprintf(stdout, "Size: %d\n", bits1);

	memset(hist, 0, sizeof(hist));
	memset(hist1, 0, sizeof(hist1));
	memset(hist2, 0, sizeof(hist2));

	last = -1;
	sumlast = 0;
	count = 0;
	one = 0;
	ratio = 0.5;
	for (i = 0; i < bits1; i++) {
		if (dcts1[i] >= -127 && 
		    dcts1[i] <= 127)
			hist1[dcts1[i] + 128]++;
		
		if (dcts2[i] >= -127 && 
		    dcts2[i] <= 127)
			hist2[dcts2[i] + 128]++;

		if (abs(dcts2[i]) & 1)
			one++;
		
		if ((i % 500) == 0 && i) {
			ratio = 0.7*ratio + 0.3*((float)one/500);
			one = 0;
		}

		if (dcts1[i] != dcts2[i]) {
			ushort first, second;

			count++;

			/* jphide */
			if (scans & FLAG_DOJPHIDE) {
				first = dcts1[i] > 0 ? dcts1[i] : -dcts1[i];
				second = dcts2[i] > 0 ? dcts2[i] : -dcts2[i];
			} else {
				/* outguess */
				first = dcts1[i];
				second = dcts2[i];
			}
			   
			sumlast += last >= 0 ? i - last : 0;
			if ((scans & FLAG_HIST) == 0) {
				fprintf(stdout,
				    "% 9d: % 5d != % 5d: %0x | % 5d : %7.2f| %4.2f\n",
				    i, dcts1[i], dcts2[i], first ^ second,
				    last >= 0 ? i - last : 0,
				    (float)sumlast/count,
				    ratio);
			}
			last = i;

			if (dcts1[i] >= -127 && 
			    dcts1[i] <= 127)
				hist[dcts1[i] + 128]++;

			if (scans & FLAG_DOJPHIDE) {
				memcpy(shist, hist, sizeof(shist));
				memcpy(shist1, hist1, sizeof(shist1));
				memcpy(shist2, hist2, sizeof(shist2));
			}
		}
	}

	if (scans & FLAG_DOJPHIDE) {
		memcpy(hist, shist, sizeof(hist));
		memcpy(hist1, shist1, sizeof(hist1));
		memcpy(hist2, shist2, sizeof(hist2));
	}

	if (scans & FLAG_HIST) {
		for (i = 0; i <= 256; i++)
			fprintf(stdout, "% 4d % 5d % 5d\n",
			    i - 128, hist1[i], hist2[i]);
		goto out;
	}

	fprintf(stdout, "Total changes: %d bits, %d bytes\n",
		count, count/8);

	for (i = 0; i <= 256; i++)
		list[i] = i;

	qsort(list, sizeof(list)/sizeof(int), sizeof(int),
	      compare);

	fprintf(stdout, "Histogram of changes to coefficients:\n");
	for (i = 0; i <= 256; i++)
		if (hist[list[i]])
			fprintf(stdout,
				"% 4d: % 5d(%.3f) | %6.3f%% || % 7d | % 7d : %7.3f%%\n",
			    list[i] - 128,
			    hist[list[i]], (float)hist[list[i]]/hist1[list[i]],
			    (float)hist[list[i]]/count * 100,
			    hist1[list[i]], hist2[list[i]],
			    (float)(hist2[list[i]] - hist1[list[i]])/
			    hist1[list[i]] * 100);

	fprintf(stdout, "Histogram of changes to coefficients (in order):\n");
	for (i = 0; i <= 256; i++)
		if (hist[i])
			fprintf(stdout,
				"% 4d: % 5d(%.3f) | %6.3f%% || % 7d | % 7d : %7.3f%%\n",
			    i - 128,
			    hist[i], (float)hist[i]/hist1[i],
			    (float)hist[i]/count * 100,
			    hist1[i], hist2[i],
			    (float)(hist2[i] - hist1[i])/
			    hist1[i] * 100);

 out:
	if (dcts1)
		free(dcts1);
	if (dcts2)
		free(dcts2);
}

int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int i, ch;

	progname = argv[0];

	/* read command line arguments */
	while ((ch = getopt(argc, argv, "Vht:")) != -1)
		switch((char)ch) {
		case 'h':
			scans |= FLAG_HIST;
			break;
		case 'V':
			fprintf(stdout, "Stegcompare Version %s\n", VERSION);
			exit(1);
		case 't':
			scans &= ~(FLAG_DOOUTGUESS|FLAG_DOJPHIDE);
			for (i = 0; i < strlen(optarg) && !scans; i++)
				switch(optarg[i]) {
				case 'o':
					scans |= FLAG_DOOUTGUESS;
					break;
				case 'p':
					scans |= FLAG_DOJPHIDE;
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

	argc -= optind;
	argv += optind;

	if (argc != 2) {
		usage();
		exit(1);
	}
	
	setvbuf(stdout, NULL, _IOLBF, 0);

	docompare(argv[0], argv[1]);

	exit(0);
}
