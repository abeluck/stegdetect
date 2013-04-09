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

void
usage(void)
{
	fprintf(stderr,	"Usage: %s orig.jpg deimages.jpg\n", progname);
}

void
dodeimage(char *file1, char *file2)
{
	int comp, row, col, i;
	short val, tval;
	extern void *dctcoeff;
	extern struct jpeg_decompress_struct jinfo;
	extern JBLOCKARRAY dctcompbuf[];
	JBLOCKARRAY dctbuf[3];
	extern int hib[], wib[];
	int ohib[3], owib[3];
	struct jpeg_compress_struct dst;
	struct jpeg_error_mgr dsterr;
	FILE *fp;

	if (jpg_open("/home/stego_analysis/compress/dscf0033.jpg") == -1)
		return;

	for (comp = 0; comp < 3; comp++) {
		ohib[comp] = hib[comp];
		owib[comp] = wib[comp];

		dctbuf[comp] = dctcompbuf[comp];
	}

	/* Open first file */
	if (jpg_open(file1) == -1)
		return;

	if ((fp = fopen(file2, "w")) == NULL) {
		warn("fopen");
		goto out;
	}

	for (comp = 0; comp < 3; comp++) 
		for (row = 0 ; row < hib[comp]; row++)
			for (col = 0; col < wib[comp]; col++)
				for (i = 0; i < DCTSIZE2; i++) {
					if (!comp && !row && !col && i <= 7)
						continue;

					val = dctcompbuf[comp][row][col][i];

					if (val == 0)
						continue;

					tval = dctbuf[comp][row % ohib[comp]][col % owib[comp]][i];

					if (val == -1 || val == 1) {
						if (tval < 0)
							val = -1;
						else 
							val = 1;
					} else {
						if (val < 0)
							val = 0 - val;
						val &= 0x3;
						
						if (tval < 0) {
							tval = -tval;
							tval &= ~0x3;
							if (tval == 0)
								tval = 4;
							val |= tval;
							val = -val;
						} else {
							tval &= ~0x3;
							if (tval == 0)
								tval = 4;
							val |= tval;
						}
					}

					dctcompbuf[comp][row][col][i] = val;
				}

	dst.err = jpeg_std_error(&dsterr);
	jpeg_create_compress(&dst);

	jpeg_copy_critical_parameters(&jinfo, &dst);
	dst.optimize_coding = TRUE;
	jpeg_stdio_dest(&dst, fp);
	jpeg_write_coefficients(&dst, dctcoeff);
	jpeg_finish_compress(&dst);
	jpeg_destroy_compress(&dst);

 out:
	jpg_finish();
	jpg_destroy();
}

int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	char ch;

	progname = argv[0];

	/* read command line arguments */
	while ((ch = getopt(argc, argv, "V")) != -1)
		switch((char)ch) {
		case 'V':
			fprintf(stdout, "Stegdeimage Version %s\n", VERSION);
			exit(1);
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

	dodeimage(argv[0], argv[1]);

	exit(0);
}
