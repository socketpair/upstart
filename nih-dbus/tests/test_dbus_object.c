/* libnih
 *
 * test_dbus_object.c - test suite for nih-dbus/dbus_object.c
 *
 * Copyright © 2009 Scott James Remnant <scott@netsplit.com>.
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

#include <nih/test.h>
#include <nih-dbus/test_dbus.h>

#include <dbus/dbus.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/error.h>

#include <nih-dbus/dbus_message.h>
#include <nih-dbus/dbus_connection.h>
#include <nih-dbus/dbus_object.h>


static int             foo_called = FALSE;
static NihDBusObject * last_object = NULL;
static NihDBusMessage *last_message = NULL;
static DBusConnection *last_message_conn = NULL;

static DBusHandlerResult
foo_handler (NihDBusObject * object,
	     NihDBusMessage *message)
{
	foo_called = TRUE;
	last_object = object;
	last_message = message;
	last_message_conn = message->conn;

	TEST_FREE_TAG (message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static int bar_called = FALSE;

static DBusHandlerResult
bar_handler (NihDBusObject * object,
	     NihDBusMessage *message)
{
	bar_called = TRUE;
	last_object = object;
	last_message = message;
	last_message_conn = message->conn;

	TEST_FREE_TAG (message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static int colour_get_called = FALSE;
static int colour_set_called = FALSE;

static int
colour_get (NihDBusObject *  object,
	    NihDBusMessage * message,
	    DBusMessageIter *iter)
{
	DBusMessageIter subiter;
	const char *    str_value;

	colour_get_called = TRUE;
	last_object = object;
	last_message = message;
	last_message_conn = message->conn;

	TEST_FREE_TAG (message);

	if (! dbus_message_iter_open_container (iter, DBUS_TYPE_VARIANT,
						DBUS_TYPE_STRING_AS_STRING,
						&subiter))
		return -1;

	str_value = "blue";
	if (! dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
					      &str_value))
		return -1;

	if (! dbus_message_iter_close_container (iter, &subiter))
		return -1;

	return 0;
}

static DBusHandlerResult
colour_set (NihDBusObject *  object,
	    NihDBusMessage * message,
	    DBusMessageIter *iter)
{
	DBusMessageIter subiter;
	const char *    value;

	colour_set_called = TRUE;
	last_object = object;
	last_message = message;
	last_message_conn = message->conn;

	TEST_FREE_TAG (message);

	TEST_EQ (dbus_message_iter_get_arg_type (iter),
		 DBUS_TYPE_VARIANT);

	dbus_message_iter_recurse (iter, &subiter);

	TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
		 DBUS_TYPE_STRING);

	dbus_message_iter_get_basic (&subiter, &value);
	TEST_EQ_STR (value, "red");

	return DBUS_HANDLER_RESULT_HANDLED;
}

static int size_get_called = FALSE;

static int
size_get (NihDBusObject *  object,
	  NihDBusMessage * message,
	  DBusMessageIter *iter)
{
	DBusMessageIter subiter;
	dbus_uint32_t   uint32_value;

	size_get_called = TRUE;
	last_object = object;
	last_message = message;
	last_message_conn = message->conn;

	TEST_FREE_TAG (message);

	if (! dbus_message_iter_open_container (iter, DBUS_TYPE_VARIANT,
						DBUS_TYPE_UINT32_AS_STRING,
						&subiter))
		return -1;

	uint32_value = 34;
	if (! dbus_message_iter_append_basic (&subiter, DBUS_TYPE_UINT32,
					      &uint32_value))
		return -1;

	if (! dbus_message_iter_close_container (iter, &subiter))
		return -1;

	return 0;
}

static int poke_set_called = FALSE;

static DBusHandlerResult
poke_set (NihDBusObject *  object,
	  NihDBusMessage * message,
	  DBusMessageIter *iter)
{
	poke_set_called = TRUE;
	last_object = object;
	last_message = message;
	last_message_conn = message->conn;

	TEST_FREE_TAG (message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static const NihDBusArg foo_args[] = {
	{ "str", "s", NIH_DBUS_ARG_IN },
	{ "len", "u", NIH_DBUS_ARG_IN },
	{ "count", "u", NIH_DBUS_ARG_OUT },
	{ NULL }
};

static const NihDBusArg bar_args[] = {
	{ "wibble", "d", NIH_DBUS_ARG_IN },
	{ NULL }
};

static const NihDBusArg baz_args[] = {
	{ NULL }
};

static const NihDBusArg signal_args[] = {
	{ "msg", "s", NIH_DBUS_ARG_IN },
	{ NULL }
};

static const NihDBusMethod interface_a_methods[] = {
	{ "Foo", foo_handler, foo_args },
	{ "Bar", bar_handler, bar_args },
	{ NULL }
};

static const NihDBusSignal interface_a_signals[] = {
	{ "Alert", signal_args },
	{ "Panic", signal_args },
	{ NULL }
};

static const NihDBusMethod interface_b_methods[] = {
	{ "Bar", foo_handler, bar_args },
	{ "Baz", foo_handler, baz_args },
	{ NULL }
};

static const NihDBusProperty interface_b_props[] = {
	{ "Colour", "s", NIH_DBUS_READWRITE, colour_get, colour_set },
	{ "Size",   "u", NIH_DBUS_READ,      size_get,   NULL },
	{ "Poke",   "d", NIH_DBUS_WRITE,     NULL,       poke_set },
	{ NULL }
};

static const NihDBusProperty interface_c_props[] = {
	{ "Colour", "u", NIH_DBUS_READWRITE, size_get, poke_set },
	{ "Height", "u", NIH_DBUS_READ,      size_get, NULL },
	{ NULL }
};

static const NihDBusInterface interface_a = {
	"Nih.TestA",
	interface_a_methods,
	interface_a_signals,
	NULL
};

static const NihDBusInterface interface_b = {
	"Nih.TestB",
	interface_b_methods,
	NULL,
	interface_b_props
};

static const NihDBusInterface interface_c = {
	"Nih.TestC",
	NULL,
	NULL,
	interface_c_props
};

static const NihDBusInterface *no_interfaces[] = {
	NULL
};

static const NihDBusInterface *one_interface[] = {
	&interface_a,
	NULL
};

static const NihDBusInterface *prop_interface[] = {
	&interface_b,
	NULL
};

static const NihDBusInterface *all_interfaces[] = {
	&interface_a,
	&interface_b,
	&interface_c,
	NULL
};

void
test_object_new (void)
{
	pid_t           dbus_pid;
	DBusConnection *conn;
	NihDBusObject * object;

	/* Check that we can register a new object, having the filled in
	 * structure returned for us with the object registered against
	 * the connection at the right path.
	 */
	TEST_FUNCTION ("nih_dbus_object_new");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (conn);

	TEST_ALLOC_FAIL {
		void *data;

		object = nih_dbus_object_new (NULL, conn, "/com/netsplit/Nih",
					      all_interfaces, &object);

		if (test_alloc_failed) {
			TEST_EQ_P (object, NULL);

			continue;
		}

		TEST_ALLOC_SIZE (object, sizeof (NihDBusObject));

		TEST_ALLOC_PARENT (object->path, object);
		TEST_EQ_STR (object->path, "/com/netsplit/Nih");

		TEST_EQ_P (object->conn, conn);
		TEST_EQ_P (object->data, &object);
		TEST_EQ_P (object->interfaces, all_interfaces);
		TEST_EQ (object->registered, TRUE);

		TEST_TRUE (dbus_connection_get_object_path_data (
				   conn, "/com/netsplit/Nih", &data));
		TEST_EQ_P (data, object);

		nih_free (object);
	}

	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}

void
test_object_destroy (void)
{
	pid_t           dbus_pid;
	DBusConnection *conn;
	NihDBusObject * object;
	void *          data;

	/* Check that a registered D-Bus object is unregistered from the
	 * bus when it is destroyed.
	 */
	TEST_FUNCTION ("nih_dbus_object_destroy");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (conn);

	dbus_connection_set_exit_on_disconnect (conn, FALSE);

	object = nih_dbus_object_new (NULL, conn, "/com/netsplit/Nih",
				      all_interfaces, &object);
	assert (object != NULL);
	assert (dbus_connection_get_object_path_data (
			conn, "/com/netsplit/Nih", &data));
	assert (data == object);

	nih_free (object);

	TEST_TRUE (dbus_connection_get_object_path_data (
			   conn, "/com/netsplit/Nih", &data));
	TEST_EQ_P (data, NULL);

	TEST_DBUS_CLOSE (conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}

void
test_object_unregister (void)
{
	pid_t           dbus_pid;
	DBusConnection *conn;
	NihDBusObject * object;

	/* Check that when a D-Bus connection is destroyed, any registered
	 * D-Bus objects go as well.
	 */
	TEST_FUNCTION ("nih_dbus_object_unregister");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (conn);

	dbus_connection_set_exit_on_disconnect (conn, FALSE);

	object = nih_dbus_object_new (NULL, conn, "/com/netsplit/Nih",
				      all_interfaces, &object);
	assert (object != NULL);

	TEST_FREE_TAG (object);

	TEST_DBUS_CLOSE (conn);

	TEST_FREE (object);


	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_object_message (void)
{
	pid_t            dbus_pid;
	DBusConnection * server_conn;
	DBusConnection * client_conn;
	NihDBusObject *  object;
	DBusMessage *    message;
	dbus_uint32_t    serial;
	DBusMessage *    reply;

	TEST_FUNCTION ("nih_dbus_object_message");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that the handler for a known method is called with the
	 * object passed in along with a message structure containing
	 * both the message and connection (which will be freed before
	 * returning.
	 */
	TEST_FEATURE ("with registered method");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      one_interface, &server_conn);

	TEST_ALLOC_FAIL {
		foo_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			"Nih.TestA",
			"Foo");
		assert (message != NULL);

		assert (dbus_connection_send (client_conn, message, NULL));
		dbus_connection_flush (client_conn);

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_TRUE (foo_called);
		TEST_EQ_P (last_object, object);
		TEST_FREE (last_message);
		TEST_EQ_P (last_message_conn, server_conn);
	}

	nih_free (object);


	/* Check that the first of two handlers for a method without a
	 * specified interface is called.
	 */
	TEST_FEATURE ("with method registered to multiple interfaces");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		foo_called = FALSE;
		bar_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			NULL,
			"Bar");
		assert (message != NULL);

		assert (dbus_connection_send (client_conn, message, NULL));
		dbus_connection_flush (client_conn);

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (foo_called);
		TEST_TRUE (bar_called);
		TEST_EQ_P (last_object, object);
		TEST_FREE (last_message);
		TEST_EQ_P (last_message_conn, server_conn);
	}

	nih_free (object);


	/* Check that an unknown method on a known interface results in
	 * an error being returned to the caller.
	 */
	TEST_FEATURE ("with unknown method on known interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		foo_called = FALSE;
		bar_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			"Nih.TestB",
			"Wibble");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_FALSE (foo_called);
		TEST_FALSE (bar_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that an unknown method on an unknown interface results in
	 * an error being returned to the caller.
	 */
	TEST_FEATURE ("with unknown method on unknown interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		foo_called = FALSE;
		bar_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			"Nih.TestC",
			"Wibble");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_FALSE (foo_called);
		TEST_FALSE (bar_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that an unknown method with no specified interface results in
	 * an error being returned to the caller.
	 */
	TEST_FEATURE ("with unknown method with no interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		foo_called = FALSE;
		bar_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			NULL,
			"Wibble");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_FALSE (foo_called);
		TEST_FALSE (bar_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that a method call when no interfaces are specified results
	 * in an error being returned to the caller.
	 */
	TEST_FEATURE ("with method call and no interfaces");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      no_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		foo_called = FALSE;
		bar_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			"Nih.TestA",
			"Foo");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_FALSE (foo_called);
		TEST_FALSE (bar_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_object_introspect (void)
{
	pid_t            dbus_pid;
	DBusConnection * server_conn;
	DBusConnection * client_conn;
	NihDBusObject *  object;
	NihDBusObject *  child1;
	NihDBusObject *  child2;
	DBusMessage *    message;
	dbus_uint32_t    serial;
	DBusMessage *    reply;
	const char *     xml;

	TEST_FUNCTION ("nih_dbus_object_introspect");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that the Introspect message is handled internally with
	 * an accurate portrayal of the interfaces and their properties
	 * returned.
	 */
	TEST_FEATURE ("with fully-fledged object");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_INTROSPECTABLE,
			"Introspect");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		TEST_TRUE (dbus_message_has_signature (reply, "s"));

		TEST_TRUE (dbus_message_get_args (reply, NULL,
						  DBUS_TYPE_STRING, &xml,
						  DBUS_TYPE_INVALID));

		TEST_EQ_STRN (xml, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
		xml += strlen (DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);

		TEST_EQ_STRN (xml, "<node name=\"/com/netsplit/Nih\">\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\"Nih.TestA\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Foo\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"str\" type=\"s\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"len\" type=\"u\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"count\" type=\"u\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Bar\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"wibble\" type=\"d\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <signal name=\"Alert\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"msg\" type=\"s\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </signal>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <signal name=\"Panic\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"msg\" type=\"s\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </signal>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\"Nih.TestB\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Bar\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"wibble\" type=\"d\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Baz\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <property name=\"Colour\" type=\"s\" access=\"readwrite\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <property name=\"Size\" type=\"u\" access=\"read\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <property name=\"Poke\" type=\"d\" access=\"write\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\"Nih.TestC\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <property name=\"Colour\" type=\"u\" access=\"readwrite\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <property name=\"Height\" type=\"u\" access=\"read\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\""
			      DBUS_INTERFACE_PROPERTIES "\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Get\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Set\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"value\" type=\"v\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"GetAll\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\""
			      DBUS_INTERFACE_INTROSPECTABLE "\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Introspect\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "</node>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STR (xml, "");

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that the Introspect message does not include the
	 * Properties interfaces in the output if none of the interfaces
	 * implement properties.
	 */
	TEST_FEATURE ("with no properties");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      one_interface, &server_conn);

	TEST_ALLOC_FAIL {
		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_INTROSPECTABLE,
			"Introspect");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		TEST_TRUE (dbus_message_has_signature (reply, "s"));

		TEST_TRUE (dbus_message_get_args (reply, NULL,
						  DBUS_TYPE_STRING, &xml,
						  DBUS_TYPE_INVALID));

		TEST_EQ_STRN (xml, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
		xml += strlen (DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);

		TEST_EQ_STRN (xml, "<node name=\"/com/netsplit/Nih\">\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\"Nih.TestA\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Foo\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"str\" type=\"s\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"len\" type=\"u\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"count\" type=\"u\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Bar\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"wibble\" type=\"d\" direction=\"in\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <signal name=\"Alert\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"msg\" type=\"s\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </signal>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <signal name=\"Panic\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"msg\" type=\"s\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </signal>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\""
			      DBUS_INTERFACE_INTROSPECTABLE "\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Introspect\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "</node>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STR (xml, "");

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that the Introspect message works when there are no
	 * interfaces.
	 */
	TEST_FEATURE ("with no interfaces");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      no_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_INTROSPECTABLE,
			"Introspect");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		TEST_TRUE (dbus_message_has_signature (reply, "s"));

		TEST_TRUE (dbus_message_get_args (reply, NULL,
						  DBUS_TYPE_STRING, &xml,
						  DBUS_TYPE_INVALID));

		TEST_EQ_STRN (xml, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
		xml += strlen (DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);

		TEST_EQ_STRN (xml, "<node name=\"/com/netsplit/Nih\">\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Introspect\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "</node>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STR (xml, "");

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that the Introspect message contains node entries for
	 * children, but doesn't bother to flesh them out.
	 */
	TEST_FEATURE ("with children nodes");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      no_interfaces, &server_conn);
	child1 = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih/Frodo",
				      one_interface, &server_conn);
	child2 = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih/Bilbo",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_INTROSPECTABLE,
			"Introspect");
		assert (message != NULL);

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);
		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		TEST_TRUE (dbus_message_has_signature (reply, "s"));

		TEST_TRUE (dbus_message_get_args (reply, NULL,
						  DBUS_TYPE_STRING, &xml,
						  DBUS_TYPE_INVALID));

		TEST_EQ_STRN (xml, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
		xml += strlen (DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);

		TEST_EQ_STRN (xml, "<node name=\"/com/netsplit/Nih\">\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    <method name=\"Introspect\">\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "    </method>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  </interface>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "  <node name=\"Bilbo\"/>\n");
		xml = strchr (xml, '\n') + 1;
		TEST_EQ_STRN (xml, "  <node name=\"Frodo\"/>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STRN (xml, "</node>\n");
		xml = strchr (xml, '\n') + 1;

		TEST_EQ_STR (xml, "");

		dbus_message_unref (reply);
	}

	nih_free (child2);
	nih_free (child1);
	nih_free (object);


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_object_property_get (void)
{
	pid_t            dbus_pid;
	DBusConnection * server_conn;
	DBusConnection * client_conn;
	NihDBusObject *  object;
	DBusMessage *    message;
	DBusMessageIter  iter;
	DBusMessageIter  subiter;
	const char *     interface_name;
	const char *     property_name;
	dbus_uint32_t    serial;
	DBusMessage *    reply;
	const char *     str_value;

	TEST_FUNCTION ("nih_dbus_object_property_get");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can get the value of the property, with the
	 * actual reply handled internally but the variant appended to
	 * the message.
	 */
	TEST_FEATURE ("with known property");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      prop_interface, &server_conn);

	TEST_ALLOC_FAIL {
		colour_get_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Get");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "Nih.TestB";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Colour";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_TRUE (colour_get_called);
		TEST_EQ_P (last_object, object);
		TEST_FREE (last_message);
		TEST_EQ_P (last_message_conn, server_conn);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		TEST_TRUE (dbus_message_has_signature (reply, "v"));

		dbus_message_iter_init (reply, &iter);
		dbus_message_iter_recurse (&iter, &subiter);

		TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
			 DBUS_TYPE_STRING);

		dbus_message_iter_get_basic (&subiter, &str_value);

		TEST_EQ_STR (str_value, "blue");

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that the first of two properties with the same name
	 * but on different interfaces is used when the property interface
	 * is not given.
	 */
	TEST_FEATURE ("with property registered to multiple interfaces");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_get_called = FALSE;
		size_get_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Get");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Colour";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_TRUE (colour_get_called);
		TEST_FALSE (size_get_called);
		TEST_EQ_P (last_object, object);
		TEST_FREE (last_message);
		TEST_EQ_P (last_message_conn, server_conn);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_EQ (dbus_message_get_type (reply),
			 DBUS_MESSAGE_TYPE_METHOD_RETURN);
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		TEST_TRUE (dbus_message_has_signature (reply, "v"));

		dbus_message_iter_init (reply, &iter);
		dbus_message_iter_recurse (&iter, &subiter);

		TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
			 DBUS_TYPE_STRING);

		dbus_message_iter_get_basic (&subiter, &str_value);

		TEST_EQ_STR (str_value, "blue");

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that attempting to get an unknown property on a known
	 * interface results in an error reply.
	 */
	TEST_FEATURE ("with unknown property on known interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_get_called = FALSE;
		size_get_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Get");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "Nih.TestB";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Height";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_get_called);
		TEST_FALSE (size_get_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that a property on an unknown interface always results in
	 * an error reply.
	 */
	TEST_FEATURE ("with unknown property on unknown interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_get_called = FALSE;
		size_get_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Get");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "Nih.FooBar";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Colour";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_get_called);
		TEST_FALSE (size_get_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that an unknown property when no interface was specified
	 * results in an error reply.
	 */
	TEST_FEATURE ("with unknown property with no interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_get_called = FALSE;
		size_get_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Get");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Width";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_get_called);
		TEST_FALSE (size_get_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that an error reply is always received when no interfaces
	 * were defined.
	 */
	TEST_FEATURE ("with no interfaces");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      no_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_get_called = FALSE;
		size_get_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Get");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Width";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_get_called);
		TEST_FALSE (size_get_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_object_property_set (void)
{
	pid_t            dbus_pid;
	DBusConnection * server_conn;
	DBusConnection * client_conn;
	NihDBusObject *  object;
	DBusMessage *    message;
	DBusMessageIter  iter;
	DBusMessageIter  subiter;
	const char *     interface_name;
	const char *     property_name;
	dbus_uint32_t    serial;
	DBusMessage *    reply;
	const char *     str_value;

	TEST_FUNCTION ("nih_dbus_object_property_set");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can set the value of the property, with the
	 * registered setter function being called to do so with the
	 * right value.
	 */
	TEST_FEATURE ("with known property");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      prop_interface, &server_conn);

	TEST_ALLOC_FAIL {
		colour_set_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Set");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "Nih.TestB";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Colour";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		assert (dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter));

		str_value = "red";
		assert (dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value));

		assert (dbus_message_iter_close_container (&iter, &subiter));

		assert (dbus_connection_send (client_conn, message, NULL));
		dbus_connection_flush (client_conn);

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_TRUE (colour_set_called);
		TEST_EQ_P (last_object, object);
		TEST_FREE (last_message);
		TEST_EQ_P (last_message_conn, server_conn);
	}

	nih_free (object);


	/* Check that the first of two properties with the same name
	 * but on different interfaces is used when the property interface
	 * is not given.
	 */
	TEST_FEATURE ("with property registered to multiple interfaces");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_set_called = FALSE;
		poke_set_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Set");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Colour";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		assert (dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter));

		str_value = "red";
		assert (dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value));

		assert (dbus_message_iter_close_container (&iter, &subiter));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_TRUE (colour_set_called);
		TEST_FALSE (poke_set_called);
		TEST_EQ_P (last_object, object);
		TEST_FREE (last_message);
		TEST_EQ_P (last_message_conn, server_conn);
	}

	nih_free (object);


	/* Check that attempting to set an unknown property on a known
	 * interface results in an error reply.
	 */
	TEST_FEATURE ("with unknown property on known interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_set_called = FALSE;
		poke_set_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Set");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "Nih.TestB";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Height";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		assert (dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter));

		str_value = "red";
		assert (dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value));

		assert (dbus_message_iter_close_container (&iter, &subiter));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_set_called);
		TEST_FALSE (poke_set_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that a property on an unknown interface always results in
	 * an error reply.
	 */
	TEST_FEATURE ("with unknown property on unknown interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_set_called = FALSE;
		poke_set_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Set");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "Nih.FooBar";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Colour";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		assert (dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter));

		str_value = "red";
		assert (dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value));

		assert (dbus_message_iter_close_container (&iter, &subiter));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_set_called);
		TEST_FALSE (poke_set_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that an unknown property when no interface was specified
	 * results in an error reply.
	 */
	TEST_FEATURE ("with unknown property with no interface");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      all_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_set_called = FALSE;
		poke_set_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Set");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Width";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		assert (dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter));

		str_value = "red";
		assert (dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value));

		assert (dbus_message_iter_close_container (&iter, &subiter));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_set_called);
		TEST_FALSE (poke_set_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	/* Check that an error reply is always received when no interfaces
	 * were defined.
	 */
	TEST_FEATURE ("with no interfaces");
	object = nih_dbus_object_new (NULL, server_conn, "/com/netsplit/Nih",
				      no_interfaces, &server_conn);

	TEST_ALLOC_FAIL {
		colour_set_called = FALSE;
		poke_set_called = FALSE;
		last_object = NULL;
		last_message = NULL;
		last_message_conn = NULL;

		message = dbus_message_new_method_call (
			dbus_bus_get_unique_name (server_conn),
			"/com/netsplit/Nih",
			DBUS_INTERFACE_PROPERTIES,
			"Set");
		assert (message != NULL);

		dbus_message_iter_init_append (message, &iter);

		interface_name = "";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&interface_name));

		property_name = "Width";
		assert (dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
							&property_name));

		assert (dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter));

		str_value = "red";
		assert (dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value));

		assert (dbus_message_iter_close_container (&iter, &subiter));

		TEST_ALLOC_SAFE {
			assert (dbus_connection_send (client_conn, message, &serial));
			dbus_connection_flush (client_conn);
		}

		dbus_message_unref (message);

		TEST_DBUS_DISPATCH (server_conn);

		TEST_FALSE (colour_set_called);
		TEST_FALSE (poke_set_called);
		TEST_EQ_P (last_object, NULL);
		TEST_EQ_P (last_message_conn, NULL);

		TEST_DBUS_MESSAGE (client_conn, reply);

		TEST_TRUE (dbus_message_is_error (reply, DBUS_ERROR_UNKNOWN_METHOD));
		TEST_EQ (dbus_message_get_reply_serial (reply), serial);

		dbus_message_unref (reply);
	}

	nih_free (object);


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


int
main (int   argc,
      char *argv[])
{
	nih_error_init ();

	test_object_new ();
	test_object_destroy ();
	test_object_unregister ();
	test_object_message ();
	test_object_introspect ();
	test_object_property_get ();
	test_object_property_set ();

	return 0;
}
