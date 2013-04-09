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
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/queue.h>

#include "config.h"

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <err.h>
#include <unistd.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <event.h>

#include "xsteg.h"

#define FLAG_DOOUTGUESS	0x0001
#define FLAG_DOJPHIDE	0x0002
#define FLAG_DOJSTEG	0x0004
#define FLAG_DOINVIS	0x0008
#define FLAG_DOF5	0x0010

extern int (*event_sigcb)(void);
extern int event_gotsig;

struct event start_ev;
struct event gtk_ev;
int quit = 0;
int change = 0;
int adding = 0;		/* Set while adding new files to the task */
int stopped = 0;	/* When adding this indicates that we stopped */

pid_t sd_pid = -1;
int chld_stdin, chld_stdout, chld_stderr;
struct event ev_stdin, ev_stdout, ev_stderr;

int scans = FLAG_DOOUTGUESS|FLAG_DOJPHIDE|FLAG_DOJSTEG|FLAG_DOINVIS|FLAG_DOF5;
float sensitivity = 1.0;

struct filequeue filelist;

/* Widget to output text to */
GtkWidget *text, *stop, *filemenu_open;
GtkWidget *clist;
gint sorttype[2] = {-1, -1};

/* Fileselector memory */
char stored_dirname[1024];

GdkColormap *cmap;
GdkColor red, green;
GtkStyle *default_style, *red_style, *green_style;


void
clist_column_clicked(GtkCList *clist, gint column)
{
	if (sorttype[column] == -1)
		sorttype[column] = GTK_SORT_ASCENDING;
	else if (sorttype[column] == GTK_SORT_ASCENDING)
		sorttype[column] = GTK_SORT_DESCENDING;
	else
		sorttype[column] = GTK_SORT_ASCENDING;

	gtk_clist_set_sort_type(GTK_CLIST(clist), sorttype[column]);
	gtk_clist_set_sort_column(GTK_CLIST(clist), column);
	gtk_clist_sort(GTK_CLIST(clist));
}

/* Terminate the process */

void
stop_clicked(GtkWidget *widget, gpointer data)
{
	if (adding)
		stopped = 1;

	filename_flush();

	stegdetect_stop();
}

void
sensitivity_change(GtkWidget *widget, gpointer data)
{
	gchar *text;
	float tmp;

	text = gtk_entry_get_text(GTK_ENTRY(widget));

	if (sscanf(text, "%f", &tmp) != 1)
		return;

	sensitivity = tmp;

	if (sd_pid != -1)
		change = 1;
}

void
select_scan(GtkWidget *widget, gpointer data)
{
	char *name = data;
	int which;

	if (!strcmp(name, "jsteg"))
		which = FLAG_DOJSTEG;
	else if (!strcmp(name, "jphide"))
		which = FLAG_DOJPHIDE;
	else if (!strcmp(name, "outguess"))
		which = FLAG_DOOUTGUESS;
	else if (!strcmp(name, "invisible"))
		which = FLAG_DOINVIS;
	else if (!strcmp(name, "f5"))
		which = FLAG_DOF5;
	else
		return;
	
	if (GTK_TOGGLE_BUTTON (widget)->active) 
	{
		/* If control reaches here, the toggle button is down */
		scans |= which;
	} else {
		/* If control reaches here, the toggle button is up */
		scans &= ~which;
	}

	if (scans == 0) {
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), 1);
		scans |= which;
	}

	/* Something in the settings has changed */
	if (sd_pid != -1)
		change = 1;
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

int
filename_adddir(char *dirname)
{
	DIR *dir;
	struct stat sb;
	struct dirent *file;
	char fullname[PATH_MAX];
	int off;

	if (strlen(dirname) >= sizeof(fullname) - 2)
		return (0);

	dir = opendir(dirname);
	if (dir == NULL)
		return (0);
	strlcpy(fullname, dirname, sizeof(fullname));
	off = strlen(fullname);
	if (fullname[off - 1] != '/') {
		strlcat(fullname, "/", sizeof(fullname));
		off++;
	}

	while ((file = readdir(dir)) != NULL) {
		/* Process gtk events */
		while (gtk_events_pending())
			gtk_main_iteration();

		if (stopped) {
			stopped = 0;
			goto err;
		}

		if (!strcmp(file->d_name, ".") ||
		    !strcmp(file->d_name, ".."))
			continue;

		strlcpy(fullname + off, file->d_name, sizeof(fullname) - off);

		if (stat(fullname, &sb) == -1)
			continue;

		if (sb.st_mode & S_IFDIR) {
			if (filename_adddir(fullname) == -1)
				goto err;

			continue;
		}

		if (!(sb.st_mode & S_IFREG))
			continue;

		if (file_hasextension(fullname, ".jpg") ||
		    file_hasextension(fullname, ".jpeg"))
			filename_add(fullname);
	}
	closedir(dir);

	return (0);

 err:
	closedir(dir);
	return (-1);
}

int
filename_add(char *name)
{
	struct filename *fn;
	int len;

	if ((fn = malloc(sizeof(struct filename))) == NULL)
		return (-1);

	len = strlen(name) + 2;
	fn->name = malloc(len);
	if (fn->name == NULL) {
		free(fn);
		return (-1);
	}
	snprintf(fn->name, len, "%s\n", name);
	fn->change = change;
	change = 0;

	TAILQ_INSERT_TAIL(&filelist, fn, file_next);

	if (sd_pid == -1) {
		sd_pid = stegdetect_start();
		if (sd_pid == -1) {
			gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL,
			    "Could not start stegdetect - check your path.",
			    -1);
			return (-1);
		}
	}

	if (!event_pending(&ev_stdin, EV_WRITE, NULL))
		event_add(&ev_stdin, NULL);

	return (0);
}

void
filename_flush(void)
{
	struct filename *fn;

	for (fn = TAILQ_FIRST(&filelist); fn; fn = TAILQ_FIRST(&filelist)) {
		TAILQ_REMOVE(&filelist, fn, file_next);

		free(fn->name);
		free(fn);
	}
}

void
open_ok_clicked(GtkWidget *widget, gpointer data)
{
	char line[1024];
	struct stat sb;
	gchar *name, *text;
	GtkWidget *questor = data, *entry;
	int hadtext = 0;

	name = gtk_file_selection_get_filename(GTK_FILE_SELECTION(questor));

	if (!strlen(name)) {
		gtk_widget_destroy(questor);
		return;
	}

	entry = GTK_FILE_SELECTION(questor)->selection_entry;
	text = gtk_entry_get_text(GTK_ENTRY(entry));
	if (text != NULL && strlen(text))
		hadtext = 1;

	strlcpy(line, name, sizeof(line));
	gtk_widget_destroy(questor);

	if (stat(line, &sb) == -1)
		return;

	if (hadtext) {
		strlcpy(stored_dirname, dirname(line), sizeof(stored_dirname));
		strlcat(stored_dirname, "/", sizeof(stored_dirname));
	} else
		strlcpy(stored_dirname, line, sizeof(stored_dirname));


	adding = 1;
	gtk_widget_set_sensitive(filemenu_open, FALSE);

	if (sb.st_mode & S_IFREG)
		filename_add(line);
	else if (sb.st_mode & S_IFDIR)
		filename_adddir(line);

	gtk_widget_set_sensitive(filemenu_open, TRUE);
	adding = 0;
}

void
menu_activate(GtkWidget *menu)
{
	gtk_widget_set_sensitive(menu, TRUE);
}

GtkWidget *
create_help(GtkWidget *menu)
{
	GtkWidget *window, *vbox, *ok, *label, *graphic, *text, *frame;
	GtkWidget *hbox;
	GdkPixmap *logo;
	GdkBitmap *mask = NULL;

	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_widget_set_usize(window, 400, 250);
	gtk_window_set_title(GTK_WINDOW(window), "About Xsteg");
	gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, FALSE);
	gtk_window_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_widget_realize(window);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(window), vbox);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 0);

	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 3);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);

	logo = gdk_pixmap_create_from_xpm_d(window->window, &mask, NULL,
	    xsteg_xpm);
	graphic = gtk_pixmap_new(logo, mask);
	gtk_widget_show(graphic);
	gtk_container_add(GTK_CONTAINER(frame), graphic);
	gdk_pixmap_unref(logo);
	gdk_pixmap_unref(mask);

	label = gtk_label_new("Copyright 2001 Niels Provos");
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 5);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 5);

	text = gtk_text_new(NULL, NULL);
	gtk_widget_show(text);
	gtk_box_pack_start(GTK_BOX(hbox), text, TRUE, TRUE, 5);
	gtk_widget_realize(text);
	gtk_text_set_word_wrap(GTK_TEXT(text), 1);
	gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL,
	    "\n"
	    "Xsteg is a graphical frontend to stegdetect.\n"
	    "\n"
	    "Stegdetect is a utilitiy for automated detection of "
	    "steganographic content in JPG images.\n"
	    "\n"
	    "For more information go to\n"
	    "\n"
	    "\thttp://www.outguess.org/", -1);

	ok = gtk_button_new_with_label("Ok.");
	gtk_widget_show(ok);
	gtk_box_pack_start(GTK_BOX(vbox), ok, FALSE, FALSE, 0);

	gtk_signal_connect_object(GTK_OBJECT(ok), "clicked",
	    GTK_SIGNAL_FUNC(menu_activate), (gpointer) menu);

	gtk_signal_connect_object(GTK_OBJECT(ok), "clicked",
	    GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer) window);

	return (window);
}

GtkWidget *
create_file_questor(void)
{
	GtkWidget *questor;
	GtkWidget *ok;
	GtkWidget *cancel;

	questor = gtk_file_selection_new ("Select File");
	if (strlen(stored_dirname))
		gtk_file_selection_set_filename(GTK_FILE_SELECTION(questor),
		    stored_dirname);
	gtk_container_border_width(GTK_CONTAINER (questor), 10);
	GTK_WINDOW(questor)->type = GTK_WINDOW_DIALOG;

	/*
	gtk_file_selection_complete(GTK_FILE_SELECTION(questor), "*.jpg");

	gtk_clist_set_selection_mode(
		GTK_CLIST(GTK_FILE_SELECTION(questor)->file_list),
		GTK_SELECTION_MULTIPLE);
	*/

	ok = GTK_FILE_SELECTION(questor)->ok_button;
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);
	gtk_signal_connect(GTK_OBJECT (ok), "clicked",
	    GTK_SIGNAL_FUNC(open_ok_clicked), questor);

	cancel = GTK_FILE_SELECTION(questor)->cancel_button;
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);

	gtk_signal_connect_object(GTK_OBJECT(cancel), "clicked",
	    GTK_SIGNAL_FUNC(gtk_widget_destroy), (gpointer)questor);

	gtk_widget_show_all(questor);
	return (questor);
}

void
xsteg_menu_about(GtkMenuItem *item, gpointer user_data)
{
	GtkWidget *window, *menu = GTK_WIDGET(user_data);

	gtk_widget_set_sensitive(menu, FALSE);

	window = create_help(menu);
	gtk_widget_show(window);
}

void
xsteg_menu_clearneg(GtkMenuItem *item, gpointer user_data)
{
	GtkCList *list = GTK_CLIST(clist);
	gchar *text;
	gint row;

	gtk_clist_freeze(list);

	/* Remove negative rows */
	row = 0;
	while (gtk_clist_get_text(list, row, 1, &text)) {
		if (strstr(text, "negative") ||
		    strstr(text, "skipped") ||
		    strstr(text, "error")) {
			gtk_clist_remove(list, row);
			continue;
		}

		row++;
	}

	gtk_clist_thaw(list);
}

void
xsteg_menu_clear(GtkMenuItem *item, gpointer user_data)
{
	guint len;

	len = gtk_text_get_length(GTK_TEXT(text));
	gtk_text_backward_delete(GTK_TEXT(text), len);

	gtk_clist_clear(GTK_CLIST(clist));
}

void
xsteg_menu_open(GtkMenuItem *item, gpointer user_data)
{
	GtkWidget *filemenu_open;

	filemenu_open = create_file_questor();
	gtk_widget_show(filemenu_open);
}

gint
xsteg_die(GtkWidget *widget, GdkEvent  *event, gpointer data)
{
	if (sd_pid != -1)
		stegdetect_stop();

	quit = 1;
	return(FALSE);
}

GtkWidget *
create_scan_button(GtkWidget *box, char *name)
{
	GtkWidget *button;

	/* Creates a new button with the label "Button 1". */
	button = gtk_check_button_new_with_label(name);
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
	    GTK_SIGNAL_FUNC (select_scan), (gpointer) name);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	return (button);
}

GtkWidget *
create_menu_item(GtkWidget *window, GtkWidget *menu, char *name)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label(name);
	gtk_object_set_data(GTK_OBJECT(window), name, item);
	gtk_widget_show(item);
	gtk_container_add(GTK_CONTAINER(menu), item);

	return (item);
}

/* Let libevent drive gtk */

void
gtk_cb(int fd, short which, void *arg)
{
	while (gtk_events_pending())
		gtk_main_iteration();

	add_gtk_timeout();
}

void
add_gtk_timeout()
{
	struct timeval tv;

	timerclear(&tv);
	tv.tv_usec = 50000;

	if (!evtimer_initialized(&gtk_ev))
		evtimer_set(&gtk_ev, gtk_cb, NULL);

	if (!quit)
		evtimer_add(&gtk_ev, &tv);
}

void
make_colors(void)
{
	cmap = gdk_colormap_get_system();

	red.red = 0xffff;
	red.green = red.blue = 0;
	if (!gdk_color_alloc(cmap, &red)) {
		g_error("Could not allocate color: red");
	}

	green.green = 0x8fff;
	green.red = green.blue = 0x0100;
	if (!gdk_color_alloc(cmap, &green)) {
		g_error("Could not allocate color: green");
	}

	red_style = gtk_style_copy(default_style);
	red_style->fg[GTK_STATE_NORMAL] = red;

	green_style = gtk_style_copy(default_style);
	green_style->fg[GTK_STATE_NORMAL] = green;
}

char *
make_scan_arg(void)
{
	static char tmp[9];

	snprintf(tmp, sizeof(tmp), "-t%s%s%s%s%s",
	    scans & FLAG_DOJSTEG ? "j" : "",
	    scans & FLAG_DOJPHIDE ? "p" : "",
	    scans & FLAG_DOOUTGUESS ? "o" : "",
	    scans & FLAG_DOINVIS ? "i" : "",
	    scans & FLAG_DOF5 ? "f" : "");

	return (tmp);
}

void
delayed_start(int fd, short which, void *arg)
{
	sd_pid = stegdetect_start();
	if (sd_pid == -1)
		return;

	if (TAILQ_FIRST(&filelist) != NULL)
		event_add(&ev_stdin, NULL);
}

void
chld_write(int fd, short which, void *arg)
{
	struct event *ev = arg;
	struct filename *fn;

	if ((fn = TAILQ_FIRST(&filelist)) == NULL)
		return;

	if (fn->change) {
		fn->change = 0;

		/* Let the child exit after it is done */
		close(chld_stdin);
		return;
	}

	TAILQ_REMOVE(&filelist, fn, file_next);
	write(fd, fn->name, strlen(fn->name));

	/* Clear file name */
	free(fn->name);
	free(fn);

	/* Re-add event */
	if (TAILQ_FIRST(&filelist) != NULL)
		event_add(ev, NULL);
	
}

/* Response from the child */
void
chld_read(int fd, short which, void *arg)
{
	char buf[1024], *p, *start;
	struct event *ev = arg;
	ssize_t n;

	n = read(fd, buf, sizeof(buf) - 1);
	if (n == -1) {
		stegdetect_stop();
		gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL,
		    "Error: stegdetect child process died.\n", -1);
		return;
	} else
		buf[n] = '\0';

	start = p = buf;
	while ((p = strchr(p, '\n')) != NULL) {
		char *columns[2];
		gint row;
		char *nameend;

		*p = '\0';

		/* Check for a filename */
		nameend = strstr(start, " :");
		if (nameend == NULL)
			goto end;

		columns[0] = start;
		columns[1] = nameend + 3;
		*nameend = '\0';
		row = gtk_clist_append(GTK_CLIST(clist), columns);
		if (fd == chld_stderr)
			gtk_clist_set_cell_style(GTK_CLIST(clist), row, 1,
			    red_style);
		else if (strchr(nameend + 3, '('))
			gtk_clist_set_cell_style(GTK_CLIST(clist), row, 1,
			    green_style);

		/* gtk_clist_moveto(GTK_CLIST(clist), row, 0, 0, 0); */
	end:
		start = ++p;
	}

	/* Re-add the event */
	event_add(ev, NULL);
}

int
signal_cb(void)
{
	struct timeval tv;

	if (sd_pid != -1) {
		gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL,
		    "The stegdetect process terminated.  "
		    "Some files might not have been analysed.\n", -1);
	}

	stegdetect_stop();

	/* Restart the process */
	if (TAILQ_FIRST(&filelist)) {
		evtimer_set(&start_ev, delayed_start, NULL);
		timerclear(&tv);
		evtimer_add(&start_ev, &tv);
	}

	return (0);
}

void
chld_handler(int sig)
{
	sigset_t set, oldset;
	pid_t pid;
	int status;

	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, &oldset);

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		if (WIFEXITED(status) || WIFSIGNALED(status))
			event_gotsig = 1;
	}

	signal(SIGCHLD, chld_handler);

	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);
}

pid_t
stegdetect_start(void)
{
	pid_t pid;
	char line[128];
	char *which, sens[12];
	int pstdin[2], pstdout[2], pstderr[2];

	if (pipe(pstdin) == -1)
		err(1, "pipe");
	if (pipe(pstdout) == -1)
		err(1, "pipe");
	if (pipe(pstderr) == -1)
		err(1, "pipe");

	if (signal(SIGCHLD, chld_handler) == SIG_ERR)
		err(1, "signal");

	which = make_scan_arg();
	snprintf(sens, sizeof(sens), "-s%5.3f", sensitivity);

	snprintf(line, sizeof(line), "Starting stegdetect with %s %s\n",
	    which, sens);
	gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL, line, -1);

	pid = fork();
	if (pid == -1)
		err(1, "fork");

	if (pid == 0) {
		close(pstdin[1]);
		close(pstdout[0]);
		close(pstderr[0]);

		if (dup2(pstdin[0], fileno(stdin)) == -1)
			_exit(1);
		if (dup2(pstdout[1], fileno(stdout)) == -1)
			_exit(1);
		if (dup2(pstderr[1], fileno(stderr)) == -1)
			_exit(1);

		execlp("stegdetect", "stegdetect", which, sens, (void *)0);

		_exit(1);
	}

	
	close(pstdin[0]);
	close(pstdout[1]);
	close(pstderr[1]);

	chld_stdin = pstdin[1];
	chld_stdout = pstdout[0];
	chld_stderr = pstderr[0];

	event_set(&ev_stdin, chld_stdin, EV_WRITE, chld_write, &ev_stdin);
	event_set(&ev_stdout, chld_stdout, EV_READ, chld_read, &ev_stdout);
	event_set(&ev_stderr, chld_stderr, EV_READ, chld_read, &ev_stderr);

	event_add(&ev_stdout, NULL);
	event_add(&ev_stderr, NULL);

	gtk_widget_set_sensitive(stop, TRUE);

	return (pid);
}

void
stegdetect_stop(void)
{
	pid_t pid = sd_pid;

	if (sd_pid == -1)
		return;
	sd_pid = -1;

	close(chld_stdin);
	close(chld_stdout);
	close(chld_stderr);

	if (event_initialized(&ev_stdin))
		event_del(&ev_stdin);
	event_del(&ev_stdout);
	event_del(&ev_stderr);

	gtk_widget_set_sensitive(stop, FALSE);

	kill(pid, SIGINT);
}

int
main(int argc, char *argv[] )
{
	/* GtkWidget is the storage type for widgets */
	GtkWidget *window, *menu, *scroll;
	GtkWidget *vbox, *hbox, *vscroll, *vboxscans;
	GtkWidget *vboxtmp, *eventbox, *fixed;
	GtkWidget *hbox_scans, *hbox_sens, *label, *frame;
	GtkWidget *file, *options, *help;
	GtkWidget *filemenu, *filemenu_exit;
	GtkWidget *optionmenu, *optionmenu_clear, *optionmenu_clearneg;
	GtkWidget *helpmenu, *helpmenu_about;
	GtkWidget *sens_text, *separator, *button;
	GtkWidget *pane;
	GtkAdjustment *adjust;
	GtkAccelGroup *accel;
	GtkTooltips *tips;
	gchar *titles[] = {"Filename", "Detection"};

	TAILQ_INIT(&filelist);

	/* This is called in all GTK applications. Arguments are parsed
	 * from the command line and are returned to the application. */
	gtk_init (&argc, &argv);

	/* Create a new window */
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (window), "xsteg");
	gtk_window_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);

	gtk_signal_connect (GTK_OBJECT (window), "delete_event",
	    GTK_SIGNAL_FUNC (xsteg_die), NULL);

	/* Setup tool tips and accelerators */
	tips = gtk_tooltips_new();
	accel = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(window), accel);

	/* Sets the border width of the window. */
	gtk_container_set_border_width (GTK_CONTAINER (window), 10);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox);
	gtk_container_add (GTK_CONTAINER (window), vbox);

	menu = gtk_menu_bar_new ();
	gtk_object_set_data (GTK_OBJECT(window), "menu", menu);
	gtk_widget_show(menu);
	gtk_box_pack_start(GTK_BOX(vbox), menu, FALSE, TRUE, 0);

	file = create_menu_item(window, menu, "File");
	options = create_menu_item(window, menu, "Options");
	help = create_menu_item(window, menu, "Help");
	gtk_menu_item_right_justify(GTK_MENU_ITEM(help));

	/* Setup the file menu */
	filemenu = gtk_menu_new();
	gtk_widget_show(filemenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(file), filemenu);

	filemenu_open = create_menu_item(window, filemenu, "Open...");
	gtk_signal_connect(GTK_OBJECT(filemenu_open), "activate",
	    GTK_SIGNAL_FUNC(xsteg_menu_open), NULL);
	gtk_widget_add_accelerator(filemenu_open, "activate", accel,
	    GDK_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	separator = gtk_menu_item_new();
	gtk_widget_show(separator);
	gtk_container_add(GTK_CONTAINER(filemenu), separator);

	filemenu_exit = create_menu_item(window, filemenu, "Exit");
	gtk_signal_connect(GTK_OBJECT(filemenu_exit), "activate",
	    GTK_SIGNAL_FUNC(xsteg_die), NULL);

	/* Setup the options menu */
	optionmenu = gtk_menu_new();
	gtk_widget_show(optionmenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(options), optionmenu);

	optionmenu_clearneg = create_menu_item(window, optionmenu,
	    "Clear negatives");
	gtk_signal_connect(GTK_OBJECT(optionmenu_clearneg), "activate",
	    GTK_SIGNAL_FUNC(xsteg_menu_clearneg), NULL);
	gtk_widget_add_accelerator(optionmenu_clearneg, "activate", accel,
	    GDK_g, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	optionmenu_clear = create_menu_item(window, optionmenu,
	    "Clear output");
	gtk_signal_connect(GTK_OBJECT(optionmenu_clear), "activate",
	    GTK_SIGNAL_FUNC(xsteg_menu_clear), NULL);
	gtk_widget_add_accelerator(optionmenu_clear, "activate", accel,
	    GDK_n, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

	/* Setup the help menu */
	helpmenu  = gtk_menu_new();
	gtk_widget_show(helpmenu);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(help), helpmenu);

	helpmenu_about = create_menu_item(window, helpmenu, "About");
	gtk_signal_connect(GTK_OBJECT(helpmenu_about), "activate",
	    GTK_SIGNAL_FUNC(xsteg_menu_about), helpmenu_about);

	/* Box for scan type and sensitivity */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 5);

	frame = gtk_frame_new("Scan options");
	gtk_widget_show(frame);
	gtk_box_pack_start(GTK_BOX(hbox), frame, FALSE, FALSE, 2);

	hbox_scans = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox_scans);
	gtk_container_add(GTK_CONTAINER(frame), hbox_scans);

	fixed = gtk_fixed_new();
	gtk_widget_show(fixed);
	gtk_box_pack_start(GTK_BOX(hbox_scans), fixed, FALSE, FALSE, 0);
	gtk_widget_set_usize(fixed, -2, -2);
	
	eventbox = gtk_event_box_new();
	gtk_widget_show(eventbox);
	gtk_container_add(GTK_CONTAINER(fixed), eventbox);

	vboxscans = gtk_vbox_new(TRUE, 0);
	gtk_widget_show(vboxscans);
	gtk_container_add(GTK_CONTAINER(eventbox), vboxscans);

	button = create_scan_button(vboxscans, "jsteg");
	if (scans & FLAG_DOJSTEG)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 1);
	button = create_scan_button(vboxscans, "jphide");
	if (scans & FLAG_DOJPHIDE)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 1);
	button = create_scan_button(vboxscans, "outguess");
	if (scans & FLAG_DOOUTGUESS)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 1);
	button = create_scan_button(vboxscans, "invisible");
	if (scans & FLAG_DOINVIS)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 1);
	button = create_scan_button(vboxscans, "F5");
	if (scans & FLAG_DOF5)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), 1);

	gtk_tooltips_set_tip(GTK_TOOLTIPS(tips), eventbox,
	    "Adjust what schemes stegdetect should look for in images.",
	    NULL);

	separator = gtk_vseparator_new();
	gtk_widget_show(separator);
	gtk_box_pack_start(GTK_BOX(hbox_scans), separator, FALSE, FALSE, 1);

	/* Sensitivity label + entry text */
	vboxtmp = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vboxtmp);
	gtk_box_pack_start(GTK_BOX(hbox_scans), vboxtmp, FALSE, FALSE, 5);

	hbox_sens = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox_sens);
	gtk_box_pack_start(GTK_BOX(vboxtmp), hbox_sens, FALSE, FALSE, 5);

	label = gtk_label_new("Sensitivity:");
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox_sens), label, TRUE, TRUE, 10);

	adjust = (GtkAdjustment *)gtk_adjustment_new(1, 0.1, 10, 0.01, 1, 0.1);

	sens_text = gtk_spin_button_new(adjust, 0.01, 2);
	gtk_widget_show(sens_text);
	gtk_box_pack_start(GTK_BOX(hbox_sens), sens_text, FALSE, FALSE, 0);
	gtk_signal_connect(GTK_OBJECT(sens_text), "changed",
	    GTK_SIGNAL_FUNC(sensitivity_change), NULL);

	gtk_tooltips_set_tip(GTK_TOOLTIPS(tips), sens_text,
	    "A higher number makes stegdetect more sensitive to discrepancies "
	    "in images.",
	    NULL);

	separator = gtk_alignment_new(1, 0, 0, 0);
	gtk_widget_show(separator);
	gtk_container_add(GTK_CONTAINER(hbox), separator);

	/* Stop button */
	vboxtmp = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vboxtmp);
	gtk_container_add(GTK_CONTAINER(separator), vboxtmp);

	stop = gtk_button_new_with_label("Stop");
	gtk_widget_set_sensitive(stop, FALSE);
	gtk_widget_show(stop);
	gtk_box_pack_start(GTK_BOX(vboxtmp), stop, FALSE, FALSE, 5);

	gtk_signal_connect(GTK_OBJECT(stop), "clicked", 
	    GTK_SIGNAL_FUNC(stop_clicked), NULL);

	gtk_tooltips_set_tip(GTK_TOOLTIPS(tips), stop,
	    "Pressing this button stops all ongoing detection work.",
	    NULL);

	/* Output text window */
	separator = gtk_hseparator_new();
	gtk_widget_show(separator);
	gtk_box_pack_start(GTK_BOX(vbox), separator, FALSE, FALSE, 3);

	pane = gtk_vpaned_new();
	gtk_widget_show(pane);
	gtk_box_pack_start(GTK_BOX(vbox), pane, TRUE, TRUE, 1);
	gtk_widget_set_usize (pane, 500, 300);
	gtk_paned_set_gutter_size(GTK_PANED(pane), 15);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_show(scroll);
	gtk_paned_pack1(GTK_PANED(pane), scroll, TRUE, FALSE);

	clist = gtk_clist_new_with_titles(2, titles);
	gtk_widget_show(clist);
	gtk_container_add(GTK_CONTAINER(scroll), clist);
	gtk_clist_set_column_width(GTK_CLIST(clist), 0, 350);
	gtk_widget_set_usize (clist, 300, 150);
	default_style = gtk_widget_get_style(clist);
	gtk_clist_column_titles_active(GTK_CLIST(clist));

	gtk_signal_connect(GTK_OBJECT(clist), "click_column",
	    GTK_SIGNAL_FUNC(clist_column_clicked), NULL);
	gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_MULTIPLE);

	/* Set up message window */
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox);
	gtk_paned_pack2(GTK_PANED(pane), vbox, TRUE, FALSE);

	separator = gtk_alignment_new(0, 1, 0, 0);
	gtk_widget_show(separator);
	gtk_box_pack_start(GTK_BOX(vbox), separator, FALSE, FALSE, 0);

	label = gtk_label_new("Message window:");
	gtk_widget_show(label);
	gtk_container_add(GTK_CONTAINER(separator), label);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	text = gtk_text_new(NULL, NULL);
	gtk_object_set_data(GTK_OBJECT (window), "output", text);
	gtk_widget_show(text);
	gtk_box_pack_start(GTK_BOX(hbox), text, TRUE, TRUE, 0);
	gtk_text_set_word_wrap(GTK_TEXT(text), 1);
	gtk_widget_set_usize (text, 300, 15);
	gtk_widget_realize(text);
	
	vscroll = gtk_vscrollbar_new (GTK_TEXT(text)->vadj);
	gtk_object_set_data(GTK_OBJECT (window), "vscroll", vscroll);
	gtk_widget_show(vscroll);
	gtk_box_pack_end(GTK_BOX (hbox), vscroll, FALSE, FALSE, 0);

	make_colors();

	gtk_widget_show(window);

	event_init();
	event_sigcb = signal_cb;

	add_gtk_timeout();

	event_dispatch();

	if (sd_pid != -1)
		stegdetect_stop();

	return(0);
}
                        
