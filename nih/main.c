/* libnih
 *
 * main.c - main loop handling and functions often called from main()
 *
 * Copyright © 2006 Scott James Remnant <scott@netsplit.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/string.h>
#include <nih/list.h>
#include <nih/timer.h>
#include <nih/signal.h>
#include <nih/child.h>
#include <nih/io.h>
#include <nih/error.h>
#include <nih/logging.h>

#include "main.h"


/**
 * VAR_RUN:
 *
 * Directory to write pid files into.
 **/
#define VAR_RUN "/var/run"

/**
 * DEV_NULL:
 *
 * Device bound to stdin/out/err when daemonising.
 **/
#define DEV_NULL "/dev/null"


/**
 * program_name:
 *
 * The name of the program, taken from the argument array with the directory
 * name portion stripped.
 **/
const char *program_name = NULL;

/**
 * package_name:
 *
 * The name of the overall package, usually set to the autoconf PACKAGE_NAME
 * macro.  This should be used in preference.
 **/
const char *package_name = NULL;

/**
 * package_version:
 *
 * The version of the overall package, thus also the version of the program.
 * Usually set to the autoconf PACKAGE_VERSION macro.  This should be used
 * in preference.
 **/
const char *package_version = NULL;

/**
 * package_copyright:
 *
 * The copyright message for the package, taken from the autoconf
 * AC_COPYRIGHT macro.
 **/
const char *package_copyright = NULL;

/**
 * package_bugreport:
 *
 * The e-mail address to report bugs on the package to, taken from the
 * autoconf PACKAGE_BUGREPORT macro.
 **/
const char *package_bugreport = NULL;


/**
 * package_string:
 *
 * The human string for the program, either "program (version)" or if the
 * program and package names differ, "program (package version)".
 * Generated by and obtained using nih_main_package_string().
 **/
static char *package_string = NULL;


/**
 * interrupt_pipe:
 *
 * Pipe used for interrupting an active select() call in case a signal
 * comes in between the last time we handled the signal and the time we
 * ran the call.
 **/
static int interrupt_pipe[2] = { -1, -1 };

/**
 * exit_loop:
 *
 * Whether to exit the running main loop, set to TRUE by a call to
 * nih_main_loop_exit().
 **/
static __thread int exit_loop = 0;

/**
 * exit_status:
 *
 * Status to exit the running main loop with, set by nih_main_loop_exit().
 **/
static __thread int exit_status = 0;

/**
 * loop_functions:
 *
 * List of functions to be called in each main loop iteration.  Each item
 * is an NihMainLoopFunc structure.
 **/
static NihList *loop_functions = NULL;


/**
 * nih_main_init_full:
 * @argv0: program name from arguments,
 * @package: package name from configure,
 * @version: package version from configure,
 * @bugreport: bug report address from configure,
 * @copyright: package copyright message from configure.
 *
 * Should be called at the beginning of main() to initialise the various
 * global variables exported from this module.  For autoconf-using packages
 * call the nih_main_init() macro instead.
 **/
void
nih_main_init_full (const char *argv0,
		    const char *package,
		    const char *version,
		    const char *bugreport,
		    const char *copyright)
{
	nih_assert (argv0 != NULL);
	nih_assert (package != NULL);
	nih_assert (version != NULL);

	/* Only take the basename of argv0 */
	program_name = strrchr (argv0, '/');
	if (program_name) {
		program_name++;
	} else {
		program_name = argv0;
	}

	package_name = package;
	package_version = version;

	/* bugreport and copyright may be NULL/empty */
	if (bugreport && *bugreport)
		package_bugreport = bugreport;
	if (copyright && *copyright)
		package_copyright = copyright;

	if (package_string)
		nih_free (package_string);
	package_string = NULL;
}


/**
 * nih_main_package_string:
 *
 * Compares the invoked program name against the package name, producing
 * a string in the form "program (package version)" if they differ or
 * "program version" if they match.
 *
 * Returns: internal copy of the string.
 **/
const char *
nih_main_package_string (void)
{
	nih_assert (program_name != NULL);

	if (package_string)
		return package_string;

	if (strcmp (program_name, package_name)) {
		package_string = nih_sprintf (NULL, "%s (%s %s)", program_name,
					      package_name, package_version);
	} else {
		package_string = nih_sprintf (NULL, "%s %s", package_name,
					      package_version);
	}

	if (! package_string)
		return program_name;

	return package_string;
}

/**
 * nih_main_suggest_help:
 *
 * Print a message suggesting --help to stderr.
 **/
void
nih_main_suggest_help (void)
{
	nih_assert (program_name != NULL);

	fprintf (stderr, _("Try `%s --help' for more information.\n"),
		 program_name);
}

/**
 * nih_main_version:
 *
 * Print the program version to stdout.
 **/
void
nih_main_version (void)
{
	nih_assert (program_name != NULL);

	printf ("%s\n", nih_main_package_string ());
	if (package_copyright)
		printf ("%s\n", package_copyright);
	printf ("\n");
	printf (_("This is free software; see the source for copying conditions.  There is NO\n"
		  "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"));
}


/**
 * nih_main_daemonise:
 *
 * Perform the necessary steps to become a daemon process, this will only
 * return in the child process if successful.  A file will be written to
 * /var/run/<program_name>.pid containing the pid of the child process.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
nih_main_daemonise (void)
{
	pid_t pid;
	int   i, fd;

	nih_assert (program_name != NULL);


	/* Fork off child process.  This begans the detachment from our
	 * parent process; this will terminate the intermediate process.
	 */
	pid = fork ();
	if (pid < 0) {
		nih_return_system_error (-1);
	} else if (pid > 0) {
		exit (0);
	}

	/* Become session leader of a new process group, without any
	 * controlling tty.
	 */
	setsid ();

	/* When the session leader dies, SIGHUP is sent to all processes
	 * in that process group, including the child we're about to
	 * spawn.  So make damned sure it's ignored.
	 */
	nih_signal_set_ignore (SIGHUP);

	/* We now spawn off a second child (or at least attempt to),
	 * we do this so that it is guaranteed not to be a session leader,
	 * even by accident.  Therefore any open() call on a tty won't
	 * make that it's controlling terminal.
	 */
	pid = fork ();
	if (pid < 0) {
		nih_return_system_error (-1);
	} else if (pid > 0) {
		FILE *pidfile;
		char *filename;

		umask (022);

		NIH_MUST (filename = nih_sprintf (NULL, "%s/%s.pid",
						  VAR_RUN, program_name));
		pidfile = fopen (filename, "w");
		if (pidfile) {
			fprintf (pidfile, "%d\n", pid);
			fclose (pidfile);
		}

		exit (0);
	}

	/* We're now in a daemon child process.  Change our working directory
	 * and file creation mask to be more appropriate.
	 */
	chdir ("/");
	umask (0);

	/* Close the stdin/stdout/stderr that we inherited */
	for (i = 0; i < 3; i++)
		close (i);

	/* And instead bind /dev/null to them */
	fd = open (DEV_NULL, O_RDWR);
	dup (fd);
	dup (fd);

	return 0;
}


/**
 * nih_main_loop_init:
 *
 * Initialise the loop functions list.
 **/
static void
nih_main_loop_init (void)
{
	if (! loop_functions)
		NIH_MUST (loop_functions = nih_list_new (NULL));

	/* Set up the interrupt pipe, we need it to be non blocking so that
	 * we don't accidentally block if there's too many signals been
	 * triggered or something
	 */
	if (interrupt_pipe[0] == -1) {
		NIH_MUST (pipe (interrupt_pipe) == 0);

		nih_io_set_nonblock (interrupt_pipe[0]);
		nih_io_set_nonblock (interrupt_pipe[1]);

		nih_io_set_cloexec (interrupt_pipe[0]);
		nih_io_set_cloexec (interrupt_pipe[1]);
	}
}

/**
 * nih_main_loop:
 *
 * Implements a fully functional main loop for a typical process, handling
 * I/O events, signals, termination of child processes, timers, etc.
 *
 * Returns: value given to nih_main_loop_exit().
 **/
int
nih_main_loop (void)
{
	nih_main_loop_init ();

	/* Set a handler for SIGCHLD so that it can interrupt syscalls */
	nih_signal_set_handler (SIGCHLD, nih_signal_handler);

	while (! exit_loop) {
		NihTimer       *next_timer;
		struct timeval  timeout;
		fd_set          readfds, writefds, exceptfds;
		char            buf[1];
		int             nfds, ret;

		/* Use the due time of the next timer to calculate how long
		 * to spend in select().  That way we don't sleep for any
		 * less or more time than we need to.
		 */
		next_timer = nih_timer_next_due ();
		if (next_timer) {
			timeout.tv_sec = next_timer->due - time (NULL);
			timeout.tv_usec = 0;
		}

		/* Start off with empty watch lists */
		FD_ZERO (&readfds);
		FD_ZERO (&writefds);
		FD_ZERO (&exceptfds);

		/* Always look for changes in the interrupt pipe */
		FD_SET (interrupt_pipe[0], &readfds);
		nfds = interrupt_pipe[0] + 1;

		/* And look for changes in anything we're watching */
		nih_io_select_fds (&nfds, &readfds, &writefds, &exceptfds);

		/* Now we hang around until either a signal comes in (and
		 * calls nih_main_loop_interrupt), a file descriptor we're
		 * watching changes in some way or it's time to run a timer.
		 */
		ret = select (nfds, &readfds, &writefds, &exceptfds,
			      (next_timer ? &timeout : NULL));

		/* Deal with events */
		if (ret > 0)
			nih_io_handle_fds (&readfds, &writefds, &exceptfds);

		/* Deal with signals.
		 *
		 * Clear the list first so that if a signal occurs while
		 * handling signals it'll ensure that the functions get
		 * a chance to decide whether to do anything next time round
		 * without having to wait.
		 */
		while (read (interrupt_pipe[0], buf, sizeof (buf)) > 0)
			;
		nih_signal_poll ();

		/* Deal with terminated children */
		nih_child_poll ();

		/* Deal with timers */
		nih_timer_poll ();

		/* Run the loop functions */
		NIH_LIST_FOREACH_SAFE (loop_functions, iter) {
			NihMainLoopFunc *func = (NihMainLoopFunc *)iter;

			func->callback (func->data, func);
		}
	}

	exit_loop = 0;
	return exit_status;
}

/**
 * nih_main_loop_interrupt:
 *
 * Interrupts the current (or next) main loop iteration because of an
 * event that potentially needs immediate processing, or because some
 * condition of the main loop has been changed.
 **/
void
nih_main_loop_interrupt (void)
{
	nih_main_loop_init ();

	if (interrupt_pipe[1] != -1)
		write (interrupt_pipe[1], "", 1);
}

/**
 * nih_main_loop_exit:
 * @status: exit status.
 *
 * Instructs the current (or next) main loop to exit with the given exit
 * status; if the loop is in the middle of processing, it will exit once
 * all that processing is complete.
 *
 * This may be safely called by functions called by the main loop.
 **/
void
nih_main_loop_exit (int status)
{
	exit_status = status;
	exit_loop = TRUE;

	nih_main_loop_interrupt ();
}


/**
 * nih_main_loop_add_func:
 * @parent: parent of callback,
 * @callback: function to call,
 * @data: pointer to pass to @callback.
 *
 * Adds @callback to the list of functions that should be called once
 * in each main loop iteration.
 *
 * The callback structure is allocated using nih_alloc() and stored in a
 * linked list, a default destructor is set that removes the callback from
 * the list. Removal of the callback can be performed by freeing it.
 *
 * If @parent is not NULL, it should be a pointer to another allocated
 * block which will be used as the parent for this block.  When @parent
 * is freed, the returned string will be freed too.  If you have clean-up
 * that would need to be run, you can assign a destructor function using
 * the nih_alloc_set_destructor() function.
 *
 * Returns: the function information, or NULL if insufficient memory.
 **/
NihMainLoopFunc *
nih_main_loop_add_func (const void    *parent,
			NihMainLoopCb  callback,
			void          *data)
{
	NihMainLoopFunc *func;

	nih_assert (callback != NULL);

	nih_main_loop_init ();

	func = nih_new (parent, NihMainLoopFunc);
	if (! func)
		return NULL;

	nih_list_init (&func->entry);
	nih_alloc_set_destructor (func, (NihDestructor)nih_list_destructor);

	func->callback = callback;
	func->data = data;

	nih_list_add (loop_functions, &func->entry);

	return func;
}


/**
 * nih_main_term_signal:
 * @data: ignored,
 * @signal: ignored.
 *
 * Signal callback that instructs the main loop to exit with a normal
 * exit status, usually registered for SIGTERM and SIGINT for non-daemons.
 **/
void
nih_main_term_signal (void      *data,
		      NihSignal *signal)
{
	nih_main_loop_exit (0);
}
