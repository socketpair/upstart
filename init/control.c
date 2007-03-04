/* upstart
 *
 * control.c - handling of control socket requests
 *
 * Copyright © 2007 Canonical Ltd.
 * Author: Scott James Remnant <scott@ubuntu.com>.
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

#include <errno.h>
#include <unistd.h>
#include <fnmatch.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/hash.h>
#include <nih/io.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <upstart/message.h>

#include "job.h"
#include "control.h"
#include "notify.h"


/* Prototypes for static functions */
static void control_error_handler  (void  *data, NihIo *io);
static int  control_watch_jobs     (void *data, pid_t pid,
				    UpstartMessageType type);
static int  control_unwatch_jobs   (void *data, pid_t pid,
				    UpstartMessageType type);
static int  control_watch_events   (void *data, pid_t pid,
				    UpstartMessageType type);
static int  control_unwatch_events (void *data, pid_t pid,
				    UpstartMessageType type);

static int  control_job_start      (void *data, pid_t pid,
				    UpstartMessageType type, const char *name,
				    uint32_t id);
static int  control_job_stop       (void *data, pid_t pid,
				    UpstartMessageType type, const char *name,
				    uint32_t id);

static int  control_event_emit     (void *data, pid_t pid,
				    UpstartMessageType type, const char *name,
				    char **args, char **env);


/**
 * control_io:
 *
 * The NihIo being used to handle the control socket.
 **/
NihIo *control_io = NULL;

/**
 * message_handlers:
 *
 * Functions to be run when we receive particular messages from other
 * processes.  Any message types not listed here will be discarded.
 **/
static UpstartMessage message_handlers[] = {
	{ -1, UPSTART_WATCH_JOBS,
	  (UpstartMessageHandler)control_watch_jobs },
	{ -1, UPSTART_UNWATCH_JOBS,
	  (UpstartMessageHandler)control_unwatch_jobs },
	{ -1, UPSTART_WATCH_EVENTS,
	  (UpstartMessageHandler)control_watch_events },
	{ -1, UPSTART_UNWATCH_EVENTS,
	  (UpstartMessageHandler)control_unwatch_events },
	{ -1, UPSTART_JOB_START,
	  (UpstartMessageHandler)control_job_start },
	{ -1, UPSTART_JOB_STOP,
	  (UpstartMessageHandler)control_job_stop },
	{ -1, UPSTART_EVENT_EMIT,
	  (UpstartMessageHandler)control_event_emit },

	UPSTART_MESSAGE_LAST
};


/**
 * control_open:
 *
 * Opens the control socket and associates it with an NihIo structure
 * that ensures that all incoming messages are handled, outgoing messages
 * can be queued, and any errors caught and the control socket re-opened.
 *
 * Returns: NihIo for socket on success, NULL on raised error.
 **/
NihIo *
control_open (void)
{
	int sock;

	sock = upstart_open ();
	if (sock < 0)
		return NULL;

	nih_io_set_cloexec (sock);

	while (! (control_io = nih_io_reopen (NULL, sock, NIH_IO_MESSAGE,
					      (NihIoReader)upstart_message_reader,
					      NULL, control_error_handler,
					      message_handlers))) {
		NihError *err;

		err = nih_error_get ();
		if (err->number != ENOMEM) {
			nih_free (err);
			close (sock);
			return NULL;
		}

		nih_free (err);
	}

	return control_io;
}

/**
 * control_close:
 *
 * Close the currently open control socket and free the structure handling
 * it.  Any messages in the queue will be lost.
 **/
void
control_close (void)
{
	nih_assert (control_io != NULL);

	nih_io_close (control_io);
	control_io = NULL;
}

/**
 * control_error_handler:
 * @data: ignored,
 * @io: NihIo structure on which an error occurred.
 *
 * This function is called should an error occur while reading from or
 * writing to a descriptor.  We handle errors that we recognise, otherwise
 * we log them and carry on.
 **/
static void
control_error_handler (void  *data,
		       NihIo *io)
{
	NihError *err;

	nih_assert (io != NULL);
	nih_assert (io == control_io);

	err = nih_error_get ();

	switch (err->number) {
	case ECONNREFUSED: {
		NihIoMessage *message;

		/* Connection refused means that the process we're sending to
		 * has closed their socket or just died.  We don't need to
		 * error because of this, don't want to re-attempt delivery
		 * of this message and in fact don't want to send them any
		 * future notifications.
		 */
		message = (NihIoMessage *)io->send_q->next;

		notify_unsubscribe ((pid_t)message->int_data);

		nih_list_free (&message->entry);
		break;
	}
	default:
		nih_error (_("Error on control socket: %s"), err->message);
		break;
	}

	nih_free (err);
}


/**
 * control_send_job_status:
 * @pid: destination process,
 * @job: job to send.
 *
 * Sends a series of messages to @pid containing the current status of @job
 * and its processes.  The UPSTART_JOB_STATUS message is sent first, giving
 * the id and name of the job, along with its current goal and state.  Then,
 * for each active process, an UPSTART_JOB_PROCESS message is sent containing
 * the process type and current pid.  Finally an UPSTART_JOB_STATUS_END
 * message is sent.
 **/
void
control_send_job_status (pid_t  pid,
			 Job   *job)
{
	NihIoMessage *message;
	int           i;

	nih_assert (pid > 0);
	nih_assert (job != NULL);
	nih_assert (control_io != NULL);

	NIH_MUST (message = upstart_message_new (
			  control_io, pid, UPSTART_JOB_STATUS,
			  job->id, job->name, job->goal, job->state));
	nih_io_send_message (control_io, message);

	for (i = 0; i < PROCESS_LAST; i++) {
		if (job->process[i] && (job->process[i]->pid > 0)) {
			NIH_MUST (message = upstart_message_new (
					  control_io, pid, UPSTART_JOB_PROCESS,
					  i, job->process[i]->pid));
			nih_io_send_message (control_io, message);
		}
	}

	NIH_MUST (message = upstart_message_new (
			  control_io, pid, UPSTART_JOB_STATUS_END,
			  job->id, job->name, job->goal, job->state));
	nih_io_send_message (control_io, message);
}


/**
 * control_watch_jobs:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * status updates for all jobs to be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_watch_jobs (void               *data,
		    pid_t               pid,
		    UpstartMessageType  type)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_WATCH_JOBS);

	nih_info (_("Control request to subscribe %d to jobs"), pid);

	notify_subscribe_job (NULL, pid, NULL);

	return 0;
}

/**
 * control_unwatch_jobs:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * status updates for all jobs no longer be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_unwatch_jobs (void               *data,
		      pid_t               pid,
		      UpstartMessageType  type)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_UNWATCH_JOBS);

	nih_info (_("Control request to unsubscribe %d from jobs"), pid);

	sub = notify_subscription_find (pid, NOTIFY_JOB, NULL);
	if (sub)
		nih_list_free (&sub->entry);

	return 0;
}

/**
 * control_watch_events:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * notification of all events be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_watch_events (void               *data,
		      pid_t               pid,
		      UpstartMessageType  type)
{
	nih_assert (pid > 0);
	nih_assert (type == UPSTART_WATCH_EVENTS);

	nih_info (_("Control request to subscribe %d to events"), pid);

	notify_subscribe_event (NULL, pid, NULL);

	return 0;
}

/**
 * control_unwatch_events:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received.
 *
 * This function is called when another process on the system requests
 * notification of all events no longer be sent to it.  It receives no reply.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_unwatch_events (void               *data,
			pid_t               pid,
			UpstartMessageType  type)
{
	NotifySubscription *sub;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_UNWATCH_EVENTS);

	nih_info (_("Control request to unsubscribe %d from events"), pid);

	sub = notify_subscription_find (pid, NOTIFY_EVENT, NULL);
	if (sub)
		nih_list_free (&sub->entry);

	return 0;
}


/**
 * control_job_start:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to start,
 * @id: id of job to start if @name is NULL.
 *
 * This function is called when another process on the system requests that
 * we start the job named @name or with the unique @id.
 *
 * We locate the job, subscribe the process to receive notification when the
 * job state changes and when the job reaches its goal, and then initiate
 * the goal change.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_start (void                *data,
		   pid_t                pid,
		   UpstartMessageType   type,
		   const char          *name,
		   uint32_t             id)
{
	NihIoMessage *reply;
	Job          *job;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_START);

	if (name) {
		nih_info (_("Control request to start %s"), name);

		job = job_find_by_name (name);
	} else {
		nih_info (_("Control request to start job #%zu"), id);

		job = job_find_by_id (id);
	}

	/* Reply with UPSTART_JOB_UNKNOWN if we couldn't find the job,
	 * and reply with UPSTART_JOB_INVALID if the job we found by id
	 * is deleted, an instance or a replacement.
	 */
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name, id));
		nih_io_send_message (control_io, reply);
		return 0;

	} else if ((job->state == JOB_DELETED)
		   || (job->instance_of != NULL)
		   || (job->replacement_for != NULL)) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_INVALID,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	/* Obtain an instance of the job that can be started.  Make sure
	 * that this instance isn't already started, since we might never
	 * send a reply if it's already at rest.  Send UPSTART_JOB_UNCHANGED
	 * so they know their command had no effect.
	 */
	job = job_instance (job);
	if (job->goal == JOB_START) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNCHANGED,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	notify_subscribe_job (job, pid, job);

	NIH_MUST (reply = upstart_message_new (control_io, pid,
					       UPSTART_JOB,
					       job->id, job->name));
	nih_io_send_message (control_io, reply);

	job_change_goal (job, JOB_START, NULL);

	return 0;
}

/**
 * control_job_stop:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of job to stop,
 * @id: id of job to stop if @name is NULL.
 *
 * This function is called when another process on the system requests that
 * we stop the job named @name or with the unique @id.
 *
 * We locate the job, subscribe the process to receive notification when the
 * job state changes and when the job reaches its goal, and then initiate
 * the goal change.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_job_stop (void                *data,
		  pid_t                pid,
		  UpstartMessageType   type,
		  const char          *name,
		  uint32_t             id)
{
	NihIoMessage *reply;
	Job          *job;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_JOB_STOP);

	if (name) {
		nih_info (_("Control request to stop %s"), name);

		job = job_find_by_name (name);
	} else {
		nih_info (_("Control request to stop job #%zu"), id);

		job = job_find_by_id (id);
	}

	/* Reply with UPSTART_JOB_UNKNOWN if we couldn't find the job,
	 * and reply with UPSTART_JOB_INVALID if the job we found was
	 * deleted or a replacement, since we can't change those.
	 */
	if (! job) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_UNKNOWN,
						       name, id));
		nih_io_send_message (control_io, reply);
		return 0;

	} else if ((job->state == JOB_DELETED)
		   || (job->replacement_for != NULL)) {
		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB_INVALID,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);
		return 0;
	}

	if ((! job->instance) || (job->instance_of != NULL)) {
		/* Make sure that the job isn't already stopped, since we
		 * might never send a reply if it's already at rest.  Send
		 * UPSTART_JOB_UNCHANGED so they know their command had no
		 * effect.
		 */
		if (job->goal == JOB_STOP) {
			NIH_MUST (reply = upstart_message_new (
					  control_io, pid,
					  UPSTART_JOB_UNCHANGED,
					  job->id, job->name));
			nih_io_send_message (control_io, reply);
			return 0;
		}

		notify_subscribe_job (job, pid, job);

		NIH_MUST (reply = upstart_message_new (control_io, pid,
						       UPSTART_JOB,
						       job->id, job->name));
		nih_io_send_message (control_io, reply);

		job_change_goal (job, JOB_STOP, NULL);

	} else {
		int has_instance = FALSE;

		/* We've been asked to stop an instance master, we can't
		 * directly change the goal of those since they never have
		 * any running processes.  Instead of returning INVALID,
		 * we're rather more helpful, and instead stop every single
		 * instance that's running.
		 */
		NIH_HASH_FOREACH (jobs, iter) {
			Job *instance = (Job *)iter;

			if (instance->instance_of != job)
				continue;

			has_instance = TRUE;

			notify_subscribe_job (instance, pid, instance);

			NIH_MUST (reply = upstart_message_new (
					  control_io, pid, UPSTART_JOB,
					  instance->id, instance->name));
			nih_io_send_message (control_io, reply);

			job_change_goal (instance, JOB_STOP, NULL);
		}

		/* If no instances were running, we send back
		 * UPSTART_JOB_UNCHANGED since they should at least receive
		 * something for their troubles.
		 */
		if (! has_instance) {
			NIH_MUST (reply = upstart_message_new (
					  control_io, pid,
					  UPSTART_JOB_UNCHANGED,
					  job->id, job->name));
			nih_io_send_message (control_io, reply);
			return 0;
		}
	}

	return 0;
}


/**
 * control_event_emit:
 * @data: data pointer,
 * @pid: origin process id,
 * @type: message type received,
 * @name: name of event to emit,
 * @args: optional arguments to event,
 * @end: optional environment for event.
 *
 * This function is called when another process on the system requests that
 * we emit a @name event, with the optional @args and @env supplied.
 *
 * We queue the pending event and subscribe the process to receive
 * notification when the event is being handled, all changes the event makes
 * and notification when the event has finished; including whether it
 * succeeded or failed.
 *
 * If given, @args and @env are re-parented to belong to the event emitted.
 *
 * Returns: zero on success, negative value on raised error.
 **/
static int
control_event_emit (void                *data,
		    pid_t                pid,
		    UpstartMessageType   type,
		    const char          *name,
		    char               **args,
		    char               **env)
{
	EventEmission *emission;

	nih_assert (pid > 0);
	nih_assert (type == UPSTART_EVENT_EMIT);
	nih_assert (name != NULL);

	nih_info (_("Control request to emit %s event"), name);

	emission = event_emit (name, args, env);

	notify_subscribe_event (emission, pid, emission);

	return 0;
}
