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
#include <errno.h>
#include <err.h>
#include <string.h>
#include <setjmp.h>

#include <jpeglib.h>
#include <jerror.h>

#include "config.h"
#include "jphide_table.h"
#include "common.h"

struct njvirt_barray_control {
  JBLOCKARRAY mem_buffer;       /* => the in-memory buffer */
  JDIMENSION rows_in_array;     /* total virtual array height */
  JDIMENSION blocksperrow;      /* width of array (and of memory buffer) */
  JDIMENSION maxaccess;         /* max rows accessed by access_virt_barray */
  JDIMENSION rows_in_mem;       /* height of memory buffer */
  JDIMENSION rowsperchunk;      /* allocation chunk size in mem_buffer */
  JDIMENSION cur_start_row;     /* first logical row # in the buffer */
  JDIMENSION first_undef_row;   /* row # of first uninitialized row */
  boolean pre_zero;             /* pre-zero mode requested? */
  boolean dirty;                /* do current buffer contents need written? */
  boolean b_s_open;             /* is backing-store data valid? */
  jvirt_barray_ptr next;        /* link to next virtual barray control block */
  void *b_s_info;  /* System-dependent control info */
};

#define MAX_COMMENTS	10
u_char *comments[MAX_COMMENTS+1];
size_t commentsize[MAX_COMMENTS+1];
int ncomments;

u_int16_t jpg_markers = 0;

#define JPHMAXPOS	2

int jphpos[JPHMAXPOS];

typedef struct njvirt_barray_control *njvirt_barray_ptr;
njvirt_barray_ptr *dctcoeff;

JBLOCKARRAY dctcompbuf[MAX_COMPS_IN_SCAN];
int hib[MAX_COMPS_IN_SCAN], wib[MAX_COMPS_IN_SCAN];
struct jpeg_decompress_struct jinfo;

/* Comment processing */
u_char
jpeg_getc(j_decompress_ptr cinfo)
{
	struct jpeg_source_mgr *datasrc = cinfo->src;

	if (datasrc->bytes_in_buffer == 0) {
		if (! (*datasrc->fill_input_buffer) (cinfo))
			err(1, "%s: fill_input", __FUNCTION__);
	}
	datasrc->bytes_in_buffer--;
	return GETJOCTET(*datasrc->next_input_byte++);
}

METHODDEF(boolean)
marker_handler(j_decompress_ptr cinfo)
{
	u_int32_t length;
	int offset = cinfo->unread_marker - JPEG_APP0;

	jpg_markers |= 1 << offset;

	length = jpeg_getc(cinfo) << 8;
	length += jpeg_getc(cinfo);
	length -= 2;			/* discount the length word itself */

	while (length-- > 0)
		jpeg_getc(cinfo);

	return (TRUE);
}

METHODDEF(boolean)
comment_handler(j_decompress_ptr cinfo)
{
	u_int32_t length;
	u_char *p;
	
	length = jpeg_getc(cinfo) << 8;
	length += jpeg_getc(cinfo);
	length -= 2;

	p = malloc(length);
	if (p == NULL)
		return (FALSE);

	commentsize[ncomments] = length;
	comments[ncomments++] = p;

	while (length-- > 0) {
		*p++ = jpeg_getc(cinfo);
	}

	return (TRUE);
	
}

void
comments_init(void)
{
	memset(comments, 0, sizeof(comments));
	memset(commentsize, 0, sizeof(commentsize));
	ncomments = 0;
}

void
comments_free(void)
{
	int i;

	for (i = 0; i < ncomments; i++)
		free(comments[i]);
	ncomments = 0;
}

void (*stego_eoi_cb)(void *) = NULL;

void
stego_set_eoi_callback(void (*cb)(void *))
{
	stego_eoi_cb = cb;
}

void
stego_set_callback(void (*cb)(int, short), enum order order)
{
	extern void (*stego_mcu_order)(int, short);
	extern void (*stego_natural_order)(int, short);

	switch (order) {
	case ORDER_MCU:
		stego_mcu_order = cb;
		break;
	case ORDER_NATURAL:
		stego_natural_order = cb;
		break;
	}
}

short **pjdcts, *cbdcts;
int *pjbits, ncbbits;

void
jsteg_cb(int where, short val)
{
	int count;
	
	if ((val & 0x01) == val)
		return;

	count = *pjbits;
	
	if (count >= ncbbits) {
		if (ncbbits != 0)
			ncbbits *= 2;
		else
			ncbbits = 256;
		cbdcts = realloc(cbdcts, ncbbits * sizeof(short));
		if (cbdcts == NULL)
			err(1, "realloc");
		*pjdcts = cbdcts;
	}

	cbdcts[count] = val;

	(*pjbits)++;
}

int
prepare_jsteg(short **pdcts, int *pbits)
{
	pjdcts = pdcts;
	pjbits = pbits;

	stego_set_callback(jsteg_cb, ORDER_MCU);
	*pdcts = cbdcts = NULL;
	ncbbits = *pjbits = 0;

	return (0);
}

short **podcts, *cbodcts;
int *pobits, ncbobits;

void
outguess_cb(int where, short val)
{
	int count;
	
	if ((val & 0x01) == val || where == 0)
		return;

	count = *pobits;
	
	if (count >= ncbobits) {
		if (ncbobits != 0)
			ncbobits *= 2;
		else
			ncbobits = 256;
		cbodcts = realloc(cbodcts, ncbobits * sizeof(short));
		if (cbodcts == NULL)
			err(1, "realloc");
		*podcts = cbodcts;
	}

	cbodcts[count] = val;

	(*pobits)++;
}

int
prepare_outguess(short **pdcts, int *pbits)
{
	podcts = pdcts;
	pobits = pbits;

	stego_set_callback(outguess_cb, ORDER_NATURAL);
	*pdcts = cbodcts = NULL;
	ncbobits = *pobits = 0;

	return (0);
}

int
prepare_all(short **pdcts, int *pbits)
{
	int comp, row, col, val, bits, i;
	short *dcts;

	bits = 0;
	for (comp = 0; comp < 3; comp++)
		bits += hib[comp] * wib[comp] * DCTSIZE2;

	dcts = malloc(bits * sizeof (short));
	if (dcts == NULL) {
		warn("%s: malloc", __FUNCTION__);
		return (-1);
	}

	bits = 0;
	for (comp = 0; comp < 3; comp++) 
		for (row = 0 ; row < hib[comp]; row++)
			for (col = 0; col < wib[comp]; col++)
				for (i = 0; i < DCTSIZE2; i++) {
					val = dctcompbuf[comp][row][col][i];
					
					dcts[bits++] = val;
				}

	*pdcts = dcts;
	*pbits = bits;

	return (0);
}

int
prepare_all_gradx(short **pdcts, int *pbits)
{
	int comp, row, col, val, bits, i;
	short *dcts;

	bits = 0;
	for (comp = 0; comp < 3; comp++) {
		if (wib[comp] - 1 <= 0)
			errx(1, "image too small");

		bits += hib[comp] * (wib[comp] - 1) * DCTSIZE2;
	}

	dcts = malloc(bits * sizeof (short));
	if (dcts == NULL) {
		warn("%s: malloc", __FUNCTION__);
		return (-1);
	}

	bits = 0;
	for (comp = 0; comp < 3; comp++) 
		for (row = 0 ; row < hib[comp]; row++)
			for (col = 0; col < wib[comp] - 1; col++)
				for (i = 0; i < DCTSIZE2; i++) {
					val = dctcompbuf[comp][row][col][i] -
					    dctcompbuf[comp][row][col + 1][i];
					
					dcts[bits++] = val;
				}

	*pdcts = dcts;
	*pbits = bits;

	return (0);
}

int
jsteg_size(short *dcts, int bits, int *off)
{
	int count, width, len, i, mode;
	short val;

	width = mode = len = count = 0;
	for (i = 0; i < bits; i++) {
		val = dcts[i];
		
		if (mode == 0)
			width = (width << 1) | (val & 0x1);
		else
			len = (len << 1) | (val & 0x1);

		count++;

		if (mode == 0 && count >= 5) {
			mode = 1;
			count = 0;
		} else if (mode == 1 && count >= width)
			goto out;
	}

 out:
	/* Save where we left off reading the length */
	if (off != NULL)
		*off = (i + 1);
	
	return (len);
}

int
prepare_normal(short **pdcts, int *pbits)
{
	int comp, row, col, val, bits, i;
	short *dcts = NULL;

	bits = 0;
	for (comp = 0; comp < 3; comp++)
		bits += hib[comp] * wib[comp] * DCTSIZE2;

	if (pdcts != NULL) {
		dcts = malloc(bits * sizeof (short));
		if (dcts == NULL) {
			warn("%s: malloc", __FUNCTION__);
			return (-1);
		}
	}
	
	bits = 0;
	for (comp = 0; comp < 3; comp++) 
		for (row = 0 ; row < hib[comp]; row++)
			for (col = 0; col < wib[comp]; col++)
				for (i = 0; i < DCTSIZE2; i++) {
					val = dctcompbuf[comp][row][col][i];
					
					/* Skip 0 and 1 coeffs */
					if ((val & 1) == val)
						continue;

					if (dcts != NULL)
						dcts[bits] = val;
					bits++;
				}

	if (pdcts != NULL)
		*pdcts = dcts;
	*pbits = bits;

	return (0);
}

int
prepare_jphide(short **pdcts, int *pbits)
{
	int comp, val, bits, i, mbits, mode;
	int lwib[MAX_COMPS_IN_SCAN];
	int spos, nheight, nwidth, j, off;
	short *dcts = NULL;
	char *back[3];

	for (i = 0; i < 3; i++)
		lwib[i] = 64 * wib[i] - 1;

	mbits = 0;
	for (comp = 0; comp < 3; comp++) {
		int off = hib[comp] * wib[comp] * DCTSIZE2;
		mbits += off;

		/* XXX - wasteful */
		back[comp] = calloc(off, sizeof (char));
		if (back[comp] == NULL) {
			warn("%s: calloc", __FUNCTION__);
			goto err;
		}
	}

	if (pdcts != NULL) {
		dcts = malloc(mbits * sizeof (short));
		if (dcts == NULL) {
			warn("%s: malloc", __FUNCTION__);
			goto err;
		}
	}

	comp = ltab[0];
	spos = ltab[1];
	mode = ltab[2];
	nheight = 0;
	nwidth = spos - 64;
	bits = j = 0;

	while (bits < mbits) {
		nwidth += DCTSIZE2;
		if (nwidth > lwib[comp]) {
			nwidth = spos;
			nheight++;
			if (nheight >= hib[comp]) {
				if (j == 0)
					jphpos[0] = bits;
				if (j == 3)
					jphpos[1] = bits;
				j += 3;
				if (ltab[j] < 0)
					goto out;

				comp = ltab[j];
				nwidth = spos = ltab[j + 1];
				mode = ltab[j + 2];
				nheight = 0;
			}
		}
		/* Protect IV */
		if (comp == 0 && nheight == 0 && nwidth <= 7)
			continue;

		val = dctcompbuf[comp][nheight][nwidth / DCTSIZE2][nwidth % DCTSIZE2];

		/* Special mode checks */
		if (mode < 0) {
			/* Modifications to 2-LSB */
			continue;
		} else if (mode > 1) {
			/* Modifications to everything only with 1/2 or 1/4 */
			continue;
		} else if (mode && val >= -1 && val <= 1) {
			/* Modifications to -1, 0, 1 only 1/4 chance */
			continue;
		}

		/* XXX - Overwrite so that we remember where we are */
		off = nheight * wib[comp] * DCTSIZE2 + nwidth;
		if (back[comp][off])
			break;
		back[comp][off] = 1;

		if (dcts != NULL)
			dcts[bits] = val;
		bits++;
	}
 out:
	for (i = 0; i < 3; i++)
		free(back[i]);

	if (pdcts != NULL)
		*pdcts = dcts;
	*pbits = bits;

	return (0);
 err:
	for (i = 0; i < 3; i++)
		if (back[i] != NULL)
			free(back[i]);
	return (-1);
}

struct my_error_mgr {
	struct jpeg_error_mgr pub;    /* "public" fields */

	jmp_buf setjmp_buffer;        /* for return to caller */
};

typedef struct my_error_mgr * my_error_ptr;

/*
 * Here's the routine that will replace the standard error_exit method:
 */

METHODDEF(void)
my_error_exit (j_common_ptr cinfo)
{
	my_error_ptr myerr = (my_error_ptr) cinfo->err;

	/* Return control to the setjmp point */
	longjmp(myerr->setjmp_buffer, 1);
}

METHODDEF(void)
my_error_emit (j_common_ptr cinfo, int level)
{
	j_decompress_ptr dinfo = (j_decompress_ptr)cinfo;
	if (cinfo->err->msg_code != JTRC_EOI)
		return;

	if (dinfo->src->bytes_in_buffer == 0) {
		dinfo->src->fill_input_buffer(dinfo);

		/* If we get only two bytes, its a fake EOI from library */
		if (dinfo->src->bytes_in_buffer == 2)
			return;
	}

	/* Give the information to the user */
	(*stego_eoi_cb)(dinfo);
}

void
jpg_finish(void)
{
	jpeg_finish_decompress(&jinfo);
	comments_free();
}

void
jpg_destroy(void)
{
	jpeg_destroy_decompress(&jinfo);
	comments_free();
}

void
jpg_version(int *major, int *minor, u_int16_t *markers)
{
	*major = jinfo.JFIF_major_version;
	*minor = jinfo.JFIF_minor_version;
	*markers = jpg_markers;
}

int
jpg_toimage(char *filename, struct image *image)
{
	JSAMPARRAY buf;
	int rowstep;
	struct jpeg_decompress_struct jinfo;
	struct jpeg_error_mgr jerr;
	FILE *fin;

	if ((fin = fopen(filename, "r")) == NULL) {
		int error = errno;

		fprintf(stderr, "%s : error: %s\n",
			filename, strerror(error));
		return (-1);
	}

	jinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&jinfo);
	jpeg_stdio_src(&jinfo, fin);
	jpeg_read_header(&jinfo, TRUE);

	jinfo.do_fancy_upsampling = FALSE;
	jinfo.do_block_smoothing = FALSE;

	jpeg_start_decompress(&jinfo);

	image->x = jinfo.output_width;
	image->y = jinfo.output_height;
	image->depth = jinfo.output_components;
	image->max = 255;

	image->img = malloc(jinfo.output_width * jinfo.output_height *
	    jinfo.output_components);

	if (image->img == NULL)
		err(1, "%s: malloc", __FUNCTION__);

	rowstep = jinfo.output_width * jinfo.output_components;

	buf = (jinfo.mem->alloc_sarray)((j_common_ptr) &jinfo, JPOOL_IMAGE, rowstep, 1);

	while (jinfo.output_scanline < jinfo.output_height) {
		jpeg_read_scanlines(&jinfo, buf, 1);

		memcpy(&image->img[(jinfo.output_scanline-1)*rowstep],
		    buf[0], rowstep);
	}

	fclose(fin);
	jpeg_finish_decompress(&jinfo);
	jpeg_destroy_decompress(&jinfo);

	return (0);
}

int
jpg_open(char *filename)
{
	char outbuf[1024];
	int i;
	struct my_error_mgr jerr;
	jpeg_component_info *compptr;
	FILE *fin;

	comments_init();
	jpg_markers = 0;
	
	if ((fin = fopen(filename, "r")) == NULL) {
		int error = errno;

		fprintf(stderr, "%s : error: %s\n",
			filename, strerror(error));
		return (-1);
	}

	jinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;
	if (stego_eoi_cb != NULL)
		jerr.pub.emit_message = my_error_emit;
	/* Establish the setjmp return context for my_error_exit to use. */
	if (setjmp(jerr.setjmp_buffer)) {
		/* Always display the message. */
		(*jinfo.err->format_message) ((j_common_ptr)&jinfo, outbuf);

		fprintf(stderr, "%s : error: %s\n", filename, outbuf);

		/* If we get here, the JPEG code has signaled an error.
		 * We need to clean up the JPEG object, close the input file,
		 * and return.
		 */
		jpeg_destroy_decompress(&jinfo);

		fclose(fin);
		return (-1);
	}
	jpeg_create_decompress(&jinfo);
	jpeg_set_marker_processor(&jinfo, JPEG_COM, comment_handler);
	for (i = 1; i < 16; i++)
		jpeg_set_marker_processor(&jinfo, JPEG_APP0+i, marker_handler);
	jpeg_stdio_src(&jinfo, fin);
	jpeg_read_header(&jinfo, TRUE);

	/* jinfo.quantize_colors = TRUE; */
	dctcoeff = (njvirt_barray_ptr *)jpeg_read_coefficients(&jinfo);

	fclose(fin);

	if (dctcoeff == NULL) {
		fprintf(stderr, "%s : error: can not get coefficients\n",
		    filename);
		goto out;
	}

	if (jinfo.out_color_space != JCS_RGB) {
		fprintf(stderr, "%s : error: is not a RGB image\n", filename);
		goto out;
	}
 
	i = jinfo.num_components;
	if (i != 3) {
		fprintf(stderr,
			"%s : error: wrong number of color components: %d\n",
			filename, i);
		goto out;
	}
	
	for(i = 0; i < 3; i++) {
		compptr = jinfo.cur_comp_info[i];
		/*
		fprintf(stderr,
			"input_iMCU_row: %d, v_samp_factor: %d\n",
			jinfo.input_iMCU_row * compptr->v_samp_factor,
			(JDIMENSION) compptr->v_samp_factor);

		fprintf(stderr, "hib: %d, wib: %d\n",
			jinfo.comp_info[i].height_in_blocks,
			jinfo.comp_info[i].width_in_blocks);
		*/

		wib[i] = jinfo.comp_info[i].width_in_blocks;
		hib[i] = jinfo.comp_info[i].height_in_blocks;
		dctcompbuf[i] = dctcoeff[i]->mem_buffer;
	}

	return (0);
out:
	jpg_destroy();

	return (-1);
}

int
file_hasextension(char *name, char *ext)
{
	int nlen = strlen(name);
	int elen = strlen(ext);

	if (nlen < elen)
		return (0);

	return (strcasecmp(name + nlen - elen, ext) == 0);
}

#define NBUCKETS	64

u_char table[256];

int
is_random(u_char *buf, int size)
{
	static int initalized;
	u_char *p, val;
	int bucket[NBUCKETS];
	int i, j, one;
	float tmp, sum, exp, ratio;

	if (!initalized) {
		table[0] = 0; table[1] = 1; table[2] = 1; table[3] = 2;
		table[4] = 1; table[5] = 2; table[6] = 2; table[7] = 3;

		for (i = 8; i < 16; i++)
			table[i] = 1 + table[i & 0x7];
		for (i = 16; i < 256; i++)
			table[i] = table[(i >> 4) & 0xf] + table[i & 0xf];

		initalized = 1;
	}
	
	one = 0;
	for (i = 0; i < size; i++)
		one += table[buf[i]];

	ratio = (float)one/(size * 8);
#if BREAKOG_DEBUG
	fprintf(stderr, "One: %5.2f, Zero: %5.2f\n",
	    ratio * 100, (1-ratio) * 100);
#endif
	if (ratio < 0.46 || ratio > 0.54)
		return (0);
	/* Chi^2 Test */
	memset(bucket, 0, sizeof(bucket));
	one = buf[0];
	val = 0;
	p = &buf[1];
	for (j = 0; j < (size-1)*8; j++) {
		bucket[one & (NBUCKETS-1)]++;
		if ((j % 8) == 0)
			val = *p++;
		one >>= 1;
		one |= (val & 0x80);
		val <<= 1;
	}

	exp = (float)j/NBUCKETS;
	sum = 0;
	for (i = 0; i < NBUCKETS; i++) {
		tmp = (bucket[i] - exp)*(bucket[i] - exp);
		sum += tmp/exp;
	}

#if BREAKOG_DEBUG
	fprintf(stderr, "Chi^2: %8.3f\n", sum);
#endif
	if (sum > 160)
		return (0);

	return (1);
}
