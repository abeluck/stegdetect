/*
 * This file is part of John the Ripper password cracker,
 * Copyright (c) 1996-98 by Solar Designer
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <err.h>

#include "config.h"
#include "cfg.h"
#include "common.h"

struct cfg_section *cfg_database = NULL;

char *
strlwr(char *s)
{
        unsigned char *ptr = (unsigned char *)s;

        while (*ptr)
        if (*ptr >= 'A' && *ptr <= 'Z')
                *ptr++ |= 0x20;
        else
                ptr++;

        return (s);
}

char *
trim(char *s)
{
	char *e;

	while (*s && (*s == ' ' || *s == '\t')) s++;
	if (!*s) return (s);

	e = s + strlen(s) - 1;
	while (e >= s && (*e == ' ' || *e == '\t')) e--;
	*++e = 0;
	return (s);
}

void
cfg_add_section(char *name)
{
	struct cfg_section *last;

	last = cfg_database;
	cfg_database = malloc(sizeof (struct cfg_section));
	if (cfg_database == NULL)
		err(1, "malloc");

	cfg_database->next = last;

	cfg_database->name = strdup(name);
	if (cfg_database->name == NULL)
		err(1, "strdup");

	cfg_database->params = NULL;

	if (!strncmp(name, "list.", 5)) {
		cfg_database->list = malloc(sizeof(struct cfg_list));
		if (cfg_database->list == NULL)
			err(1, "malloc");
		cfg_database->list->head = cfg_database->list->tail = NULL;
	} else {
		cfg_database->list = NULL;
	}
}

void
cfg_add_line(char *line, int number)
{
	struct cfg_list *list;
	struct cfg_line *entry;

	entry = malloc(sizeof(struct cfg_line));
	if (entry == NULL)
		err(1, "malloc");
	entry->next = NULL;

	entry->data = strdup(line);
	if (entry->data == NULL)
		err(1, "strdup");
	entry->number = number;

	list = cfg_database->list;
	if (list->tail)
		list->tail = list->tail->next = entry;
	else
		list->tail = list->head = entry;
}

void
cfg_add_param(char *name, char *value)
{
	struct cfg_param *current, *last;

	last = cfg_database->params;
	current = cfg_database->params = malloc(sizeof (struct cfg_param));
	if (current == NULL)
		err(1, "malloc");
	current->next = last;

	current->name = strdup(name);
	current->value = strdup(value);
	if (current->name == NULL || current->value == NULL)
		err(1, "strdup");
}

int
cfg_process_line(char *line, int number)
{
	char *p;

	line = trim(line);
	if (!*line || *line == '#' || *line == ';') return (0);

	if (*line == '[') {
		if ((p = strchr(line, ']'))) *p = 0; else return (1);
		cfg_add_section(strlwr(trim(line + 1)));
	} else
	if (cfg_database && cfg_database->list) {
		cfg_add_line(line, number);
	} else
	if (cfg_database && (p = strchr(line, '='))) {
		*p++ = 0;
		cfg_add_param(strlwr(trim(line)), trim(p));
	} else {
		return (1);
	}

	return (0);
}

void
cfg_error(char *name, int number)
{
	fprintf(stderr, "Error in %s at line %d\n", name, number);
	exit(1);
}

void
cfg_init(char *name)
{
	FILE *file;
	char line[LINE_BUFFER_SIZE];
	int number;

	if (cfg_database) return;

	if (!(file = fopen(name, "r")))
		err(1, "fopen: %s", name);

	number = 0;
	while (fgetl(line, sizeof(line), file))
		if (cfg_process_line(line, ++number))
			cfg_error(name, number);

	if (ferror(file))
		err(1, "fgets");

	if (fclose(file))
		err(1, "fclose");
}

struct cfg_section *
cfg_get_section(char *section, char *subsection)
{
	struct cfg_section *current;
	char *p1, *p2;

	if ((current = cfg_database))
	do {
		p1 = current->name; p2 = section;
		while (*p1 && *p1 == tolower(*p2)) {
			p1++; p2++;
		}
		if (*p2) continue;

		if ((p2 = subsection))
		while (*p1 && *p1 == tolower(*p2)) {
			p1++; p2++;
		}
		if (*p1) continue;
		if (p2) if (*p2) continue;

		return (current);
	} while ((current = current->next));

	return (NULL);
}

struct cfg_list *
cfg_get_list(char *section, char *subsection)
{
	struct cfg_section *current;

	if ((current = cfg_get_section(section, subsection)))
		return (current->list);

	return (NULL);
}

char *
cfg_get_param(char *section, char *subsection, char *param)
{
	struct cfg_section *current_section;
	struct cfg_param *current_param;
	char *p1, *p2;

	if ((current_section = cfg_get_section(section, subsection)))
	if ((current_param = current_section->params))
	do {
		p1 = current_param->name; p2 = param;
		while (*p1 && *p1 == tolower(*p2)) {
			p1++; p2++;
		}
		if (*p1 || *p2) continue;

		return (current_param->value);
	} while ((current_param = current_param->next));

	return (NULL);
}

int
cfg_get_int(char *section, char *subsection, char *param)
{
	char *value;

	if ((value = cfg_get_param(section, subsection, param)))
		return (atoi(value));

	return (-1);
}

int
cfg_get_bool(char *section, char *subsection, char *param)
{
	char *value;

	if ((value = cfg_get_param(section, subsection, param)))
	switch (*value) {
	case 'y':
	case 'Y':
	case 't':
	case 'T':
		return (1);

	default:
		return (atoi(value));
	}

	return (0);
}
