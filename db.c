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
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "config.h"
#include "db.h"

extern int count;
extern int found;

struct dbqueue dblist;

void
db_init(void)
{
	TAILQ_INIT(&dblist);
}

void
db_insert(char *filename, int type, void *obj,
    int (*crack)(char *, char *, void *),
    int (*compare)(void *, void *),
    void (*free)(void *))
{
	struct db *db, *tmp;

	if ((db = malloc(sizeof(struct db))) == NULL)
		err(1, "malloc");

	db->filename = strdup(filename);
	if (db->filename == NULL)
		err(1, "strdup");
	db->type = type;
	db->obj = obj;
	db->crack = crack;
	db->free = free;

	if (compare != NULL) {
		for (tmp = TAILQ_FIRST(&dblist); tmp;
		     tmp = TAILQ_NEXT(tmp, next))
			if (compare(db->obj, tmp->obj) <= 0)
				break;
		if (tmp)
			TAILQ_INSERT_BEFORE(tmp, db, next);
		else
			TAILQ_INSERT_TAIL(&dblist, db, next);
	} else
		TAILQ_INSERT_TAIL(&dblist, db, next);
}

void
db_remove(struct db *db)
{
	TAILQ_REMOVE(&dblist, db, next);

	db->free(db->obj);
	free(db->filename);
	free(db);
}

void
db_flush(void)
{
	struct db *db;
	extern int quiet;

	for (db = TAILQ_FIRST(&dblist); db; db = TAILQ_FIRST(&dblist)) {
		if (!quiet)
			fprintf(stdout, "%s : negative\n", db->filename);
		db_remove(db);
	}
}

int
db_crack(char *word)
{
	struct db *db, *next;
	int res;

	for(db = TAILQ_FIRST(&dblist); db; db = next) {
		next = TAILQ_NEXT(db, next);

		count++;
		res = (*db->crack)(db->filename, word, db->obj);
		if (res) {
			found++;
			db_remove(db);
		}
	}

	if (TAILQ_FIRST(&dblist) == NULL)
		return (1);

	return (0);
}
