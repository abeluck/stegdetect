/*
 * This file is part of John the Ripper password cracker,
 * Copyright (c) 1996-98 by Solar Designer
 */

/*
 * Configuration file loader.
 */

#ifndef _JOHN_CONFIG_H
#define _JOHN_CONFIG_H

#define LINE_BUFFER_SIZE	0x400

/*
 * Parameter list entry.
 */
struct cfg_param {
	struct cfg_param *next;
	char *name, *value;
};

/*
 * Line list entry.
 */
struct cfg_line {
	struct cfg_line *next;
	char *data;
	int number;
};

/*
 * Main line list structure, head is used to start scanning the list, while
 * tail is used to add new entries.
 */
struct cfg_list {
	struct cfg_line *head, *tail;
};

/*
 * Section list entry.
 */
struct cfg_section {
	struct cfg_section *next;
	char *name;
	struct cfg_param *params;
	struct cfg_list *list;
};

/*
 * Loads a configuration file.
 */
extern void cfg_init(char *name);

/*
 * Searches for a section with the supplied name, and returns its line list
 * structure, or NULL if the search fails.
 */
extern struct cfg_list *cfg_get_list(char *section, char *subsection);

/*
 * Searches for a section with the supplied name and a parameter within the
 * section, and returns the parameter's value, or NULL if not found.
 */
extern char *cfg_get_param(char *section, char *subsection, char *param);

/*
 * Similar to the above, but does an atoi(). Returns -1 if not found.
 */
extern int cfg_get_int(char *section, char *subsection, char *param);

/*
 * Converts the value to boolean. Returns 0 (false) if not found.
 */
extern int cfg_get_bool(char *section, char *subsection, char *param);

extern char *fgetl(char *s, int size, FILE *stream);
#endif
