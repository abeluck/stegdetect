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
#include <dirent.h>

#include <jpeglib.h>
#include <file.h>

#include "config.h"
#include "common.h"
#include "cfg.h"
#include "rules.h"
#include "break_jphide.h"
#include "break_outguess.h"
#include "break_jsteg.h"
#include "db.h"

#ifndef PATH_MAX
#define PATH_MAX	1024
#endif

#define FLAG_DOOUTGUESS	0x0001
#define FLAG_DOJPHIDE	0x0002
#define FLAG_DOJSTEG	0x0004

char *rules_name;
char *progname;
char *wordlist = "/usr/share/dict/words";

int convert = 0;
int quiet = 0;
int alarmed = 0;
int signaled = 0;
FILE *word_file;
int line_number, rule_number, rule_count;

u_int32_t count, total_count;
int found = 0;
struct timeval last_tv;
time_t starttime;

void
status_print(char *word)
{
	struct stat stat;
	long where;
	int part_file;
	struct timeval tv, rtv;
	float rate = 0;

	if (!word_file || word_file == stdin) {
		fprintf(stderr, "Status: %s\n", word);
		return;
	}

	if (fstat(fileno(word_file), &stat))
		err(1, "fstat");
	if ((where = ftell(word_file)) == -1)
		err(1, "ftell");

	part_file = where * 100 / (stat.st_size + 1);

	gettimeofday(&tv, NULL);
	timersub(&tv, &last_tv, &rtv); 
	if (rtv.tv_sec)
		rate = (float)count/rtv.tv_sec;
	last_tv = tv;
	total_count += count;
	count = 0;

	fprintf(stderr, "Status: % 7.3f%%, % 8.1f c/s: %s\n",
		(float)(rule_number * 100 + part_file) / rule_count,
		rate,
		word);
}

void
usage(void)
{
	fprintf(stderr,
		"Usage: %s [-V] [-r <rules>] [-f <wordlist>] [-t <schemes>] file.jpg ...\n",
		progname);
}

void
sig_handle_timer(int sig)
{
	alarmed = 1;
}

void
sig_handle_inter(int sig)
{
	signaled = 1;
	signal(SIGINT, SIG_DFL);
}

char *
do_wordlist_crack(char *name)
{
	char line[LINE_BUFFER_SIZE];
	struct rpp_context ctx;
	int length;
	char *rule, *word = NULL;
	char last[RULE_WORD_SIZE];
	int rules = 1;

	if (name) {
		if (!(word_file = fopen(name, "r")))
			err(1, "fopen: %s", name);
	} else
		word_file = stdin;

	length = 16;

	if (rpp_init(&ctx, SUBSECTION_WORDLIST)) {
		fprintf(stderr, "No wordlist mode rules found in %s\n",
			rules_name);
		exit(1);
	}

	rules_init(length);
	rule_count = rules_count(&ctx, -1);

	line_number = rule_number = 0;

	rule = rpp_next(&ctx);

	memset(last, ' ', length + 1);
	last[length + 2] = 0;

	alarmed = signaled = 0;
	signal(SIGALRM, sig_handle_timer);
	signal(SIGINT, sig_handle_inter);

	total_count += count;
	count = 0;
	gettimeofday(&last_tv, NULL);

	if (rule)
		do {
			if ((rule = rules_reject(rule, NULL)))
				while (fgetl(line, sizeof(line), word_file)) {
					line_number++;

					if (signaled) {
						alarm(1);
						signaled = 0;
						status_print(last);
					}
					if (alarmed) {
						signal(SIGALRM,
						       sig_handle_timer);
						signal(SIGINT,
						       sig_handle_inter);
						alarmed = 0;
					}

					if (line[0] == '#')
						if (!strncmp(line, "#!comment", 9))
							continue;

					word = rules_apply(line, rule, -1);
					if (word == NULL)
						continue;

					if (!strcmp(word, last))
						continue;

					strcpy(last, word);
			
					if (db_crack(word) == 1) {
						rules = 0;
						break;
					}
				}

			if (rules) {
				if (!(rule = rpp_next(&ctx))) break;
				rule_number++;

				line_number = 0;
				if (fseek(word_file, 0, SEEK_SET))
					err(1, "fseek");
			}
		} while (rules);

	if (ferror(word_file))
		err(1, "fgets");

	if (name) {
		if (fclose(word_file))
			err(1, "fclose");
		word_file = NULL;
	}

	alarm(0);
	signal(SIGALRM, SIG_DFL);
	signal(SIGINT, SIG_DFL);

	db_flush();

	return (!rules ? word : NULL);
}

/*
 * Converts a JPEG image into a short file that can be used
 * for the dictionary attack instead of the image.
 */

int
doconvert(char *filename, char *extension, void *obj,
    int (*obj_write)(char *, void *), void (*obj_destroy)(void *))
{
	char name[1024], *p;

	strlcpy(name, filename, sizeof(name));
	p = strrchr(name, '.');
	if (p != NULL) {
		*p = '\0';
		strlcat(name, extension, sizeof(name));
	}

	if (p == NULL || obj_write(name, obj) == -1) {
		fprintf(stderr, "%s: can not convert\n", filename);
		return (-1);
	}

	if (!quiet)
		fprintf(stderr, "%s: converted to %s\n", filename, extension);

	obj_destroy(obj);

	return (0);
}

void *
outguess_read_jpg(char *filename)
{
	void *obj;
	short *dcts = NULL;
	int res, bits;

	prepare_outguess(&dcts, &bits);
		
	res = jpg_open(filename);
	stego_set_callback(NULL, ORDER_NATURAL);
		
	if (res == -1) {
		if (dcts != NULL)
			free (dcts);
		return (NULL);
	}
		
	obj = break_outguess_prepare(dcts, bits);
		
	if (dcts != NULL)
		free(dcts);

	jpg_finish();
	jpg_destroy();

	return (obj);
}

void *
jphide_read_jpg(char *filename)
{
	void *obj;
	int bits;

	if (jpg_open(filename) == -1)
		return (NULL);

	prepare_jphide(NULL, &bits);
	obj = break_jphide_prepare(bits);

	jpg_finish();
	jpg_destroy();

	return (obj);
}

void *
jsteg_read_jpg(char *filename)
{
	void *obj;
	short *dcts = NULL;
	int res, bits;

	prepare_jsteg(&dcts, &bits);
		
	res = jpg_open(filename);
	stego_set_callback(NULL, ORDER_MCU);
		
	if (res == -1) {
		if (dcts != NULL)
			free (dcts);
		return (NULL);
	}

	obj = break_jsteg_prepare(filename, dcts, bits);
		
	if (dcts != NULL)
		free(dcts);
		
	jpg_finish();
	jpg_destroy();

	return (obj);
}

struct handler {
	int type;
	char *extension;
	int (*obj_crack)(char *, char *, void *);
	int (*obj_compare)(void *, void *);
	void (*obj_destroy)(void *);
	int (*obj_write)(char *, void *);
	void *(*obj_read)(char *);
	void *(*obj_read_jpg)(char *);
};

struct handler handlers[] = {
	{
		FLAG_DOJPHIDE, ".jph",
		crack_jphide,
		break_jphide_compare, break_jphide_destroy,
		break_jphide_write, break_jphide_read,
		jphide_read_jpg
	},
	{
		FLAG_DOOUTGUESS, ".og",
		crack_outguess,
		NULL, break_outguess_destroy,
		break_outguess_write, break_outguess_read,
		outguess_read_jpg
	},
	{
		FLAG_DOJSTEG, ".jsg",
		crack_jsteg,
		NULL, break_jsteg_destroy,
		break_jsteg_write, break_jsteg_read,
		jsteg_read_jpg
	},
};

int
doinsert(char *filename, int scans)
{
	struct handler *handle;
	int res = 0;
	void *obj;

	if (!file_hasextension(filename, ".jpg") &&
	    !file_hasextension(filename, ".jpeg")) {
		if (convert)
			return (-1);

		for (handle = &handlers[0]; handle->extension; handle++)
			if (file_hasextension(filename, handle->extension))
				break;
		if (handle->extension == NULL)
			return (-1);

		if ((obj = handle->obj_read(filename)) == NULL)
			return (-1);

		db_insert(filename, handle->type, obj,
		    handle->obj_crack, handle->obj_compare,
		    handle->obj_destroy);
	} else {
		res = -1;

		for (handle = &handlers[0]; handle->extension; handle++) {
			if (scans & handle->type) {
				obj = handle->obj_read_jpg(filename);
				if (obj == NULL)
					continue;

				if (convert)
					doconvert(filename, handle->extension,
					    obj,
					    handle->obj_write,
					    handle->obj_destroy);
				else
					db_insert(filename, handle->type, obj,
					    handle->obj_crack,
					    handle->obj_compare,
					    handle->obj_destroy);

				res = 0;
			}
		}
	}

	return (res);
}

#define MAX_FILES 1024

void
process_loop(char *name, int scans, int *pi, int *pn)
{
	int i, n;

	i = *pi;
	n = *pn;

	if (doinsert(name, scans) != -1) {
		i++;
		n++;
	}

	if (!convert && i >= MAX_FILES) {
		fprintf(stderr, "Loaded %i files...\n",
		    i);
		do_wordlist_crack(wordlist);
		i = 0;
	}

	*pi = i;
	*pn = n;
}

int
main(int argc, char *argv[])
{
	int i, n, scans;
	extern char *optarg;
	extern int optind;
	int ch;

	rules_name = RULES_NAME;
	progname = argv[0];

	scans = FLAG_DOJPHIDE;

	/* read command line arguments */
	while ((ch = getopt(argc, argv, "cqs:f:r:Vd:t:")) != -1)
		switch((char)ch) {
		case 'c':
			convert = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'r':
			rules_name = optarg;
			break;
		case 'f':
			wordlist = optarg;
			break;
		case 'V':
			fprintf(stdout, "Stegbreak Version %s\n", VERSION);
			exit(1);
		case 't':
			scans = 0;
			for (i = 0; i < strlen(optarg); i++)
				switch(optarg[i]) {
				case 'o':
					scans |= FLAG_DOOUTGUESS;
					break;
				case 'p':
					scans |= FLAG_DOJPHIDE;
					break;
				case 'j':
					scans |= FLAG_DOJSTEG;
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

	if (argc < 1) {
		usage();
		exit(1);
	}

	/* Set up magic rules */
	if (file_init())
		errx(1, "file magic initializiation failed");

        if (!convert) {
		cfg_init(rules_name);
		db_init();
	}

	setvbuf(stdout, NULL, _IOLBF, 0);

	/* Set up counters */
	total_count = count = 0;
	starttime = time(NULL);
	
	n = i = 0;
	while (argc) {
		struct stat sb;

		if (stat(argv[0], &sb) == -1)
			goto end;
		if (sb.st_mode & S_IFDIR) {
			DIR *dir;
			struct dirent *file;
			char fullname[PATH_MAX];
			int off;

			if (strlen(argv[0]) >= sizeof (fullname) - 2) {
				warnx("%s: directory name too long", argv[0]);
				goto end;
			}

			if ((dir = opendir(argv[0])) == NULL) {
				warn("%s", argv[0]);
				goto end;
			}

			strlcpy(fullname, argv[0], sizeof (fullname));
			off = strlen(fullname);
			if (fullname[off - 1] != '/') {
				strlcat(fullname, "/", sizeof(fullname));
				off++;
			}
			
			while ((file = readdir(dir)) != NULL) {
				if (!strcmp(file->d_name, ".") ||
				    !strcmp(file->d_name, ".."))
					continue;

				strlcpy(fullname + off, file->d_name,
				    sizeof(fullname) - off);
				
				process_loop(fullname, scans, &i, &n);
			}
			closedir(dir);
		} else
			process_loop(argv[0], scans, &i, &n);

	end:
		argc--;
		argv++;
	}

	if (!convert && i) {
		fprintf(stderr, "Loaded %i files...\n", i);
		do_wordlist_crack(wordlist);
	}

	if (!convert) {
		time_t now = time(NULL) - starttime;

		total_count += count;
		fprintf(stderr, "Processed %d files, found %d embeddings.\n",
			n, found);
		fprintf(stderr, "Time: %d seconds: Cracks: %d, % 8.1f c/s\n",
		    now, total_count, (float)total_count/now);
	} else
		fprintf(stderr, "Converted %d files.\n", n);

	exit(n == 0);
}
