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
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <jpeglib.h>

#include "common.h"
#include "jutil.h"
#include "dct.h"

int
f5_hkl(struct jeasy *je, short ik, short il, short val)
{
	int i, k;
	int sum = 0;
	short ***blocks = je->blocks;

	for (i = 0; i < 1 /* je->comp */; i++) {
		int presum = 0;
		int hib = je->height[i];
		int wib = je->width[i];

		for (k = 0; k < hib * wib; k++) {
			if (blocks[i][k][il * DCTSIZE + ik] == val)
					presum++;
		}

		if (je->needscale)
			presum *= je->scale[i];
		sum += presum;
	}
	return (sum);
}

struct jpeg_decompress_struct *
f5_compress(struct image *image, struct jeasy *je, int quality, FILE **pfin)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_decompress_struct *jinfo;
	static struct jpeg_error_mgr jerr, jsrcerr;
	char template[] = "/tmp/stegdetect.XXXX";
	JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
	FILE *fout, *fin;
	int row_stride;		/* physical row width in image buffer */
	int fd;

	jinfo = malloc(sizeof(struct jpeg_decompress_struct));
	if (jinfo == NULL)
		err(1, "malloc");

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	jpeg_copy_critical_parameters(je->jinfo, &cinfo);

	if ((fd = mkstemp(template)) == -1)
		err(1, "mkstemp");

	if ((fout = fdopen(fd, "w")) == NULL)
		err(1, "fdopen");

	jpeg_stdio_dest(&cinfo, fout);

	cinfo.image_width = image->x;
	cinfo.image_height = image->y;
	cinfo.input_components = image->depth;
	if (image->depth == 1)
		cinfo.in_color_space = JCS_GRAYSCALE;
	else
		cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);

	/* Take table from decompress object */
	if (quality)
		jpeg_set_quality(&cinfo, quality, TRUE);
	else {
		cinfo.quant_tbl_ptrs[0] = je->table[0];
		cinfo.quant_tbl_ptrs[0]->sent_table = FALSE;
		cinfo.quant_tbl_ptrs[1] = je->table[1];
		cinfo.quant_tbl_ptrs[1]->sent_table = FALSE;
	}

	jpeg_start_compress(&cinfo, TRUE);

	/* JSAMPLEs per row in image_buffer */
	row_stride = image->x * image->depth;

	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = & image->img[cinfo.next_scanline * row_stride];
		(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
  
	fclose(fout);
	close(fd);

	/* XXX - ugly reread */

	if ((fin = fopen(template, "r")) == NULL)
		err(1, "fopen");

	memset(jinfo, 0, sizeof(struct jpeg_decompress_struct));
	jinfo->err = jpeg_std_error(&jsrcerr);
	jpeg_create_decompress(jinfo);
	jpeg_stdio_src(jinfo, fin);

	if (!quality) {
		jpeg_read_header(jinfo, TRUE);
		jpeg_read_coefficients(jinfo);

		fclose(fin);
	} else
		*pfin = fin;

	unlink(template);

	return (jinfo);
}

void
f5_luminanceimage(struct jeasy *je, struct image *image)
{
	u_char *img;
	int i, k, l;
	int hib, wib;
	short **blocks = je->blocks[0];
	short tmp[DCTSIZE2], dq[DCTSIZE2];
	int rowspan;

	hib = je->height[0];
	wib = je->width[0];

	image->x = je->width[0] * DCTSIZE;
	image->y = je->height[0] * DCTSIZE;
	image->max = 255;
	image->depth = 1;

	rowspan = image->x * image->depth;
	
	if ((img = malloc(image->x*image->y)) == NULL)
		err(1, "malloc");

	image->img = img;

	for (k = 0; k < hib; k++) {
		for (l = 0; l < wib; l++) {
			dequant_block(dq, blocks[k*wib + l], je->table[0]);
			idct(tmp, dq);

			for (i = 0; i < DCTSIZE2; i++) {
				int x, y;
				tmp[i] += 128;

				x = l * DCTSIZE + (i % DCTSIZE);
				y = k * DCTSIZE + (i / DCTSIZE);

				if (tmp[i] > image->max)
					tmp[i] = image->max;
				if (tmp[i] < 0)
					tmp[i] = 0;

				img[y * rowspan + x] = tmp[i];
			}
		}
	}
}

struct jpeg_decompress_struct *
f5_fromfile(char *filename, FILE **pfin)
{
	struct jpeg_decompress_struct *jinfo;
	static struct jpeg_error_mgr jsrcerr;
	FILE *fin;

	if ((jinfo = calloc(1, sizeof(struct jpeg_decompress_struct))) == NULL)
		err(1, "calloc");

	if ((fin = fopen(filename, "r")) == NULL)
		err(1, ": %s", filename);

	jinfo->err = jpeg_std_error(&jsrcerr);
	jpeg_create_decompress(jinfo);
	jpeg_stdio_src(jinfo, fin);

	*pfin = fin;

	return (jinfo);
}

int
f5_decompress(struct jpeg_decompress_struct *jinfo, struct image *image)
{
	JSAMPARRAY buf;
	int rowstep;

	jpeg_read_header(jinfo, TRUE);

	jinfo->do_fancy_upsampling = FALSE;
	jinfo->do_block_smoothing = FALSE;

	jpeg_start_decompress(jinfo);

	image->x = jinfo->output_width;
	image->y = jinfo->output_height;
	image->depth = jinfo->output_components;
	image->max = 255;

	image->img = malloc(jinfo->output_width * jinfo->output_height *
	    jinfo->output_components);

	if (image->img == NULL)
		err(1, "%s: malloc", __FUNCTION__);

	rowstep = jinfo->output_width * jinfo->output_components;

	buf = (jinfo->mem->alloc_sarray)((j_common_ptr) &jinfo, JPOOL_IMAGE, rowstep, 1);

	while (jinfo->output_scanline < jinfo->output_height) {
		jpeg_read_scanlines(jinfo, buf, 1);

		memcpy(&image->img[(jinfo->output_scanline-1)*rowstep],
		    buf[0], rowstep);
	}

	jpeg_finish_decompress(jinfo);
	jpeg_destroy_decompress(jinfo);

	free(jinfo);

	return (0);
}

#define VAL(a, x, y, c)	((a)[(y)*rowspan + (x)*image->depth + c])

void
f5_blur(struct image *image, double d)
{
	int x, y, c;
	int rowspan, size;
	u_char *newimg, *img, tmp;

	size = image->y * image->x * image->depth;
	if ((newimg = malloc(size)) == NULL)
		err(1, "malloc");
	memcpy(newimg, image->img, size);

	img = image->img;
	rowspan = image->x * image->depth;
	for (y = 1; y < image->y - 1; y++) {
		for (x = 1; x < image->x - 1; x++) {
			for (c = 0; c < image->depth; c++) {
				tmp = d*VAL(img, x - 1, y, c)
				    + d*VAL(img, x + 1, y, c)
				    + d*VAL(img, x, y - 1, c)
				    + d*VAL(img, x, y + 1, c)
				    + (1 - 4*d)*VAL(img, x, y, c);
				VAL(newimg, x, y, c) = tmp;
			}
		}
	}

	free (img);
	image->img = newimg;
}

void
f5_crop(struct image *image)
{
	struct image newimage;
	int x, y, nx, ny;
	u_char *buf;
	int rowstep = image->x * image->depth;
	int depth = image->depth;

	newimage.x = image->x - 8;
	newimage.y = image->y - 8;
	newimage.depth = image->depth;
	newimage.max = image->max;

	newimage.img = malloc(newimage.x * newimage.y * newimage.depth);
	if (newimage.img == NULL)
		err(1, "malloc");

	buf = newimage.img;
	nx = ny = 0;
	for (y = 4; y < image->y - 4; y++, ny++) {
		for (x = 4; x < image->x - 4; x++, nx++) {
			memcpy(buf, &image->img[y*rowstep + x*depth], depth);
			buf += depth;
		}
	}

	free(image->img);
	*image = newimage;
}

double
betakl(struct jeasy *orig, struct jeasy *est, int k, int l)
{
	int first, second, third, fourth;
	double beta;

	first = f5_hkl(est, k, l, 1) * (f5_hkl(orig, k, l, 0)
	    - f5_hkl(est, k, l, 0) );
	second = (f5_hkl(orig, k, l, 1) - f5_hkl(est, k, l, 1))
	    * (f5_hkl(est, k, l, 2) - f5_hkl(est, k, l, 1) );

	third = f5_hkl(est, k, l, 1) ;
	third *= third;

	fourth = (f5_hkl(est, k, l, 2) - f5_hkl(est, k, l, 1));
	fourth *= fourth;

	beta = ((double)first + second)/((double)third + fourth);

	return (beta);
}

double
f5_ekl(struct jeasy *orig, struct jeasy *est, double beta, int k, int l)
{
	double first, second;
	int j;

	first = f5_hkl(orig, k, l, 0) - f5_hkl(est, k, l, 0)
	    - beta * f5_hkl(est, k, l, 1);
	first *= first;

	for (j = 1; j < 3; j++) {
		second = f5_hkl(orig, k, l, j) - (1 - beta) * f5_hkl(est, k, l, j)
		    - beta * f5_hkl(est, k, l, j + 1);
		second *= second;

		first += second;
	}

	return (first);
}

void
f5_dobeta(struct jeasy *je, struct jeasy *jne, double *pbeta, double *pekl,
    int quality, int verbose)
{
	double beta, ekl;
	double b1, b2, b3;
	int i;

	jne->needscale = 1;
	for (i = 0; i < jne->comp; i++) {
		jne->scale[i] = ((double)je->width[i]*je->height[i])/
		    ((double)jne->width[i] * jne->height[i]);
	}

	b1 = betakl(je, jne, 1, 2);
	b2 = betakl(je, jne, 2, 1);
	b3 = betakl(je, jne, 2, 2);

	beta = (b1 + b2 + b3)/3;
	ekl = f5_ekl(je, jne, beta, 1, 2)
	    + f5_ekl(je, jne, beta, 2, 1)
	    + f5_ekl(je, jne, beta, 2, 2);

	if (verbose) {
		fprintf(stderr, "Quality: %d\n", quality);

		statistic(je);
		statistic(jne);

		fprintf(stderr, "Original:\n");
		fprintf(stderr, "%d %d\n",
		    f5_hkl(je, 1, 2, 0), f5_hkl(je, 1, 2, 1));
		fprintf(stderr, "%d %d\n",
		    f5_hkl(je, 2, 1, 0), f5_hkl(je, 2, 1, 1));
		fprintf(stderr, "%d %d\n",
		    f5_hkl(je, 2, 2, 0), f5_hkl(je, 2, 2, 1));

		fprintf(stderr, "Estimated:\n");
		fprintf(stderr, "%d %d\n",
		    (int)(f5_hkl(jne, 1, 2, 0)), (int)(f5_hkl(jne, 1, 2, 1)));
		fprintf(stderr, "%d %d\n",
		    (int)(f5_hkl(jne, 2, 1, 0)), (int)(f5_hkl(jne, 2, 1, 1)));
		fprintf(stderr, "%d %d\n",
		    (int)(f5_hkl(jne, 2, 2, 0)), (int)(f5_hkl(jne, 2, 2, 1)));

		fprintf(stderr, "Result:\n");
		fprintf(stderr, "(1,2): %f\n", b1);
		fprintf(stderr, "(2,1): %f\n", b2);
		fprintf(stderr, "(2,2): %f\n", b3);

		fprintf(stderr, "Beta: %f - %f\n", beta, ekl);
	}

	*pbeta = beta;
	*pekl = ekl;
}

int f5_elim2compress = 0;

double
detect_f5(char *filename)
{
	extern struct jpeg_decompress_struct jinfo;
	struct jpeg_decompress_struct *jnew, *jtmp;
	struct image image;
	struct jeasy *je, *jne;
	double beta, ekl;
	double minbeta, minekl;
	int minquality;
	int quality, verbose = 0;
	FILE *fin;

	je = jpeg_prepare_blocks(&jinfo);

	image.img = NULL;
	if (f5_elim2compress) {
		minekl = -1;
		for (quality = 90; quality < 99; quality++) {
			/*
			  jtmp = f5_fromfile(filename, &fin);
			  f5_decompress(jtmp, &image);
			  fclose(fin);
			*/
			f5_luminanceimage(je, &image);
			f5_crop(&image);

			/* Re-compress */
			jnew = f5_compress(&image, je, quality, &fin);
			free(image.img); image.img = NULL;
			f5_decompress(jnew, &image);
			fclose(fin);

			f5_blur(&image, 0.05);

			jnew = f5_compress(&image, je, 0, NULL);
			free(image.img); image.img = NULL;
			jne = jpeg_prepare_blocks(jnew);

			f5_dobeta(je, jne, &beta, &ekl, quality, verbose);

			if (minekl == -1 || ekl < minekl) {
				minbeta = beta;
				minekl = ekl;
				minquality = quality;
			}

			jpeg_free_blocks(jne);

			jpeg_destroy_decompress(jnew);
			free(jnew);
		}
	} else {
		f5_luminanceimage(je, &image);
		f5_crop(&image);

		f5_blur(&image, 0.05);

		jnew = f5_compress(&image, je, 0, NULL);
		free(image.img);
		jne = jpeg_prepare_blocks(jnew);

		f5_dobeta(je, jne, &beta, &ekl, quality, verbose);

		minbeta = beta;

		jpeg_free_blocks(jne);

		jpeg_destroy_decompress(jnew);
		free(jnew);
	}
	jpeg_free_blocks(je);

	/* fprintf(stderr, "Beta: %f - %f, %d\n", minbeta, minekl, minquality); */

	return (minbeta);
}
