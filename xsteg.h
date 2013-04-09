#ifndef _XSTEG_H_
#define _XSTEG_H_

TAILQ_HEAD(filequeue, filename);

struct filename {
	TAILQ_ENTRY(filename) file_next;
	char *name;				/* file name to be analysed */
	int change;				/* need restart stegdetect */
};

int filename_add(char *);
void filename_flush(void);

void stegdetect_stop(void);
pid_t stegdetect_start(void);

void add_gtk_timeout(void);

void menu_activate(GtkWidget *);

extern char *xsteg_xpm[];
#endif /* _XSTEG_H_ */
