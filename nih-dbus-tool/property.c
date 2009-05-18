/* nih-dbus-tool
 *
 * property.c - property parsing and generation
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/string.h>
#include <nih/logging.h>
#include <nih/error.h>

#include <nih-dbus/dbus_object.h>

#include "symbol.h"
#include "indent.h"
#include "type.h"
#include "marshal.h"
#include "demarshal.h"
#include "property.h"
#include "interface.h"
#include "property.h"
#include "parse.h"
#include "errors.h"


/**
 * property_name_valid:
 * @name: Member name to verify.
 *
 * Verifies whether @name matches the specification for a D-Bus interface
 * member name, and thus is valid for a property.
 *
 * Returns: TRUE if valid, FALSE if not.
 **/
int
property_name_valid (const char *name)
{
	nih_assert (name != NULL);

	/* We can get away with just using strlen() here even through name
	 * is in UTF-8 because all the valid characters are ASCII.
	 */
	for (size_t i = 0; i < strlen (name); i++) {
		/* Names may contain digits, but not at the beginning. */
		if ((name[i] >= '0') && (name[i] <= '9')) {
			if (i == 0)
				return FALSE;

			continue;
		}

		/* Valid characters anywhere are [A-Za-z_] */
		if (   ((name[i] < 'A') || (name[i] > 'Z'))
		    && ((name[i] < 'a') || (name[i] > 'z'))
		    && (name[i] != '_'))
			return FALSE;
	}

	/* Name must be at least 1 character and no more than 255 characters */
	if ((strlen (name) < 1) || (strlen (name) > 255))
		return FALSE;

	return TRUE;
}


/**
 * property_new:
 * @parent: parent object for new property,
 * @name: D-Bus name of property,
 * @type: D-Bus type signature,
 * @access: access to property.
 *
 * Allocates a new D-Bus object Property data structure, with the D-Bus name
 * set to @name and the D-Bus type signature set to @type.  The returned
 * structure is not placed into any list.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned property.  When all parents
 * of the returned property are freed, the returned property will also be
 * freed.
 *
 * Returns: the new property or NULL if the allocation failed.
 **/
Property *
property_new (const void *  parent,
	      const char *  name,
	      const char *  type,
	      NihDBusAccess access)
{
	Property *property;

	nih_assert (name != NULL);

	property = nih_new (parent, Property);
	if (! property)
		return NULL;

	nih_list_init (&property->entry);

	nih_alloc_set_destructor (property, nih_list_destroy);

	property->name = nih_strdup (property, name);
	if (! property->name) {
		nih_free (property);
		return NULL;
	}

	property->symbol = NULL;

	property->type = nih_strdup (property, type);
	if (! property->type) {
		nih_free (property);
		return NULL;
	}

	property->access = access;
	property->deprecated = FALSE;

	return property;
}


/**
 * property_start_tag:
 * @xmlp: XML parser,
 * @tag: name of XML tag being parsed,
 * @attr: NULL-terminated array of attribute name and value pairs.
 *
 * This function is called by parse_start_tag() for a "property"
 * start tag, a child of the "interface" tag that defines a property the
 * D-Bus interface specifies.
 *
 * If the property does not appear within an interface tag a warning is
 * emitted and the tag will be ignored.
 *
 * Properties must have a "name" attribute containing the D-Bus name
 * of the interface, a "type" attribute containing the D-Bus type
 * signature and an "access" attribute specifying whether the property
 * is read-only, write-only or read/write.
 *
 * Any unknown attributes result in a warning and will be ignored; an
 * unknown value for the "access" attribute results in an error.
 *
 * A Property object will be allocated and pushed onto the stack, this is
 * not added to the interface until the end tag is found.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
property_start_tag (XML_Parser    xmlp,
		    const char *  tag,
		    char * const *attr)
{
	ParseContext *context;
	ParseStack *  parent;
	Property *    property;
	char * const *key;
	char * const *value;
	const char *  name = NULL;
	const char *  type = NULL;
	const char *  access_str = NULL;
	NihDBusAccess access;
	DBusError     error;

	nih_assert (xmlp != NULL);
	nih_assert (tag != NULL);
	nih_assert (attr != NULL);

	context = XML_GetUserData (xmlp);
	nih_assert (context != NULL);

	/* Properties should only appear inside interfaces. */
	parent = parse_stack_top (&context->stack);
	if ((! parent) || (parent->type != PARSE_INTERFACE)) {
		nih_warn ("%s:%zu:%zu: %s", context->filename,
			  (size_t)XML_GetCurrentLineNumber (xmlp),
			  (size_t)XML_GetCurrentColumnNumber (xmlp),
			  _("Ignored unexpected <property> tag"));

		if (! parse_stack_push (NULL, &context->stack,
					PARSE_IGNORED, NULL))
			nih_return_system_error (-1);

		return 0;
	}

	/* Retrieve the name, type and access from the attributes */
	for (key = attr; key && *key; key += 2) {
		value = key + 1;
		nih_assert (value && *value);

		if (! strcmp (*key, "name")) {
			name = *value;
		} else if (! strcmp (*key, "type")) {
			type = *value;
		} else if (! strcmp (*key, "access")) {
			access_str = *value;
		} else {
			nih_warn ("%s:%zu:%zu: %s: %s", context->filename,
				  (size_t)XML_GetCurrentLineNumber (xmlp),
				  (size_t)XML_GetCurrentColumnNumber (xmlp),
				  _("Ignored unknown <property> attribute"),
				  *key);
		}
	}

	/* Check we have a name, type and access and that they are valid */
	if (! name)
		nih_return_error (-1, PROPERTY_MISSING_NAME,
				  _(PROPERTY_MISSING_NAME_STR));
	if (! property_name_valid (name))
		nih_return_error (-1, PROPERTY_INVALID_NAME,
				  _(PROPERTY_INVALID_NAME_STR));

	if (! type)
		nih_return_error (-1, PROPERTY_MISSING_TYPE,
				  _(PROPERTY_MISSING_TYPE_STR));

	dbus_error_init (&error);
	if (! dbus_signature_validate_single (type, &error)) {
		nih_error_raise_printf (PROPERTY_INVALID_TYPE, "%s: %s",
					_(PROPERTY_INVALID_TYPE_STR),
					error.message);
		dbus_error_free (&error);
		return -1;
	}

	if (! access_str)
		nih_return_error (-1, PROPERTY_MISSING_ACCESS,
				  _(PROPERTY_MISSING_ACCESS_STR));

	if (! strcmp (access_str, "read")) {
		access = NIH_DBUS_READ;
	} else if (! strcmp (access_str, "write")) {
		access = NIH_DBUS_WRITE;
	} else if (! strcmp (access_str, "readwrite")) {
		access = NIH_DBUS_READWRITE;
	} else {
		nih_return_error (-1, PROPERTY_ILLEGAL_ACCESS,
				  _(PROPERTY_ILLEGAL_ACCESS_STR));
	}

	/* Allocate a Property object and push onto the stack */
	property = property_new (NULL, name, type, access);
	if (! property)
		nih_return_system_error (-1);

	if (! parse_stack_push (NULL, &context->stack,
				PARSE_PROPERTY, property)) {
		nih_error_raise_system ();
		nih_free (property);
		return -1;
	}

	return 0;
}

/**
 * property_end:
 * @xmlp: XML parser,
 * @tag: name of XML tag being parsed.
 *
 * This function is called by parse_end_tag() for a "property" end
 * tag, and matches a call to property_start_tag() made at the same
 * parsing level.
 *
 * The property is added to the list of properties defined by the parent
 * interface.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
property_end_tag (XML_Parser  xmlp,
		  const char *tag)
{
	ParseContext *context;
	ParseStack *  entry;
	ParseStack *  parent;
	Property *    property;
	Property *    conflict;
	Interface *   interface;

	nih_assert (xmlp != NULL);
	nih_assert (tag != NULL);

	context = XML_GetUserData (xmlp);
	nih_assert (context != NULL);

	entry = parse_stack_top (&context->stack);
	nih_assert (entry != NULL);
	nih_assert (entry->type == PARSE_PROPERTY);
	property = entry->property;

	/* Generate a symbol from the name */
	if (! property->symbol) {
		property->symbol = symbol_from_name (property, property->name);
		if (! property->symbol)
			nih_return_no_memory_error (-1);
	}

	nih_list_remove (&entry->entry);
	parent = parse_stack_top (&context->stack);
	nih_assert (parent != NULL);
	nih_assert (parent->type == PARSE_INTERFACE);
	interface = parent->interface;

	/* Make sure there's not a conflict before adding the property */
	conflict = interface_lookup_property (interface, property->symbol);
	if (conflict) {
		nih_error_raise_printf (PROPERTY_DUPLICATE_SYMBOL,
					_(PROPERTY_DUPLICATE_SYMBOL_STR),
					property->symbol, conflict->name);
		return -1;
	}

	nih_debug ("Add %s property to %s interface",
		   property->name, interface->name);
	nih_list_add (&interface->properties, &property->entry);
	nih_ref (property, interface);

	nih_free (entry);

	return 0;
}


/**
 * property_annotation:
 * @property: property object annotation applies to,
 * @name: annotation name,
 * @value: annotation value.
 *
 * Handles applying the annotation @name with value @value to the property
 * @property.  Properties may be annotated as deprecated or may have an
 * alternate symbol name specified.
 *
 * Unknown annotations or illegal values to the known annotations result
 * in an error being raised.
 *
 * Returns: zero on success, negative value on raised error.
 **/
int
property_annotation (Property *  property,
		     const char *name,
		     const char *value)
{
	nih_assert (property != NULL);
	nih_assert (name != NULL);
	nih_assert (value != NULL);

	if (! strcmp (name, "org.freedesktop.DBus.Deprecated")) {
		if (! strcmp (value, "true")) {
			nih_debug ("Marked %s property as deprecated",
				   property->name);
			property->deprecated = TRUE;
		} else if (! strcmp (value, "false")) {
			nih_debug ("Marked %s property as not deprecated",
				   property->name);
			property->deprecated = FALSE;
		} else {
			nih_return_error (-1, PROPERTY_ILLEGAL_DEPRECATED,
					  _(PROPERTY_ILLEGAL_DEPRECATED_STR));
		}

	} else if (! strcmp (name, "com.netsplit.Nih.Symbol")) {
		if (symbol_valid (value)) {
			if (property->symbol)
				nih_unref (property->symbol, property);

			property->symbol = nih_strdup (property, value);
			if (! property->symbol)
				nih_return_no_memory_error (-1);

			nih_debug ("Set %s property symbol to %s",
				   property->name, property->symbol);
		} else {
			nih_return_error (-1, PROPERTY_INVALID_SYMBOL,
					  _(PROPERTY_INVALID_SYMBOL_STR));
		}

	} else {
		nih_error_raise_printf (PROPERTY_UNKNOWN_ANNOTATION,
					"%s: %s: %s",
					_(PROPERTY_UNKNOWN_ANNOTATION_STR),
					property->name, name);
		return -1;
	}

	return 0;
}


/**
 * property_object_get_function:
 * @parent: parent object for new string,
 * @property: property to generate function for,
 * @name: name of function to generate,
 * @handler_name: name of handler function to call,
 * @prototypes: list to append function prototypes to,
 * @handlers: list to append definitions of required handlers to.
 *
 * Generates C code for a function called @name that will append a variant
 * containing the value of property @property to a D-Bus message iterator.
 * The value of the property is obtained by calling a function named
 * @handler_name, the prototype for this function is specified as a TypeFunc
 * object added to the @handlers list.
 *
 * The prototype of the function is given as a TypeFunc object appended to
 * the @prototypes list, with the name as @name itself.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are freed, the return string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
char *
property_object_get_function (const void *parent,
			      Property *  property,
			      const char *name,
			      const char *handler_name,
			      NihList *   prototypes,
			      NihList *   handlers)
{
	DBusSignatureIter   iter;
	NihList             inputs;
	NihList             locals;
	nih_local TypeFunc *func = NULL;
	TypeVar *           arg;
	nih_local TypeVar * iter_var = NULL;
	nih_local char *    code_block = NULL;
	nih_local char *    oom_error_code = NULL;
	nih_local char *    block = NULL;
	nih_local TypeFunc *handler_func = NULL;
	NihListEntry *      attrib;
	nih_local char *    vars_block = NULL;
	nih_local char *    body = NULL;
	char *              code;

	nih_assert (property != NULL);
	nih_assert (name != NULL);
	nih_assert (handler_name != NULL);
	nih_assert (prototypes != NULL);
	nih_assert (handlers != NULL);

	dbus_signature_iter_init (&iter, property->type);

	nih_list_init (&inputs);
	nih_list_init (&locals);

	/* The function returns an integer, and accepts an arguments for
	 * the D-Bus object, message and a message iterator.
	 */
	func = type_func_new (NULL, "int", name);
	if (! func)
		return NULL;

	arg = type_var_new (func, "NihDBusObject *", "object");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	arg = type_var_new (func, "NihDBusMessage *", "message");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	arg = type_var_new (func, "DBusMessageIter *", "iter");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	/* The function requires a local iterator for the variant.  Rather
	 * than deal with it by hand, it's far easier to put it on the
	 * locals list and deal with it along with the rest.
	 */
	iter_var = type_var_new (NULL, "DBusMessageIter", "variter");
	if (! iter_var)
		return NULL;

	nih_list_add (&locals, &iter_var->entry);

	/* In case of out of memory, simply return and let the caller
	 * decide what to do.
	 */
	oom_error_code = nih_strdup (NULL,
				     "dbus_message_iter_close_container (iter, &variter);\n"
				     "return -1;\n");
	if (! oom_error_code)
		return NULL;

	block = marshal (NULL, &iter, "variter", "value",
			 oom_error_code,
			 &inputs, &locals);
	if (! block)
		return NULL;

	/* Begin the handler calling block */
	if (! nih_strcat_sprintf (&code_block, NULL,
				  "/* Call the handler function */\n"
				  "if (%s (object->data, message",
				  handler_name))
		return NULL;

	handler_func = type_func_new (NULL, "int", handler_name);
	if (! handler_func)
		return NULL;

	attrib = nih_list_entry_new (handler_func);
	if (! attrib)
		return NULL;

	attrib->str = nih_strdup (attrib, "warn_unused_result");
	if (! attrib->str)
		return NULL;

	nih_list_add (&handler_func->attribs, &attrib->entry);

	arg = type_var_new (handler_func, "void *", "data");
	if (! arg)
		return NULL;

	nih_list_add (&handler_func->args, &arg->entry);

	arg = type_var_new (handler_func, "NihDBusMessage *", "message");
	if (! arg)
		return NULL;

	nih_list_add (&handler_func->args, &arg->entry);

	/* Each of the inputs to the marshalling code becomes a local
	 * variable to our function that we pass the address of to the
	 * implementation function.
	 */
	NIH_LIST_FOREACH_SAFE (&inputs, iter) {
		TypeVar *var = (TypeVar *)iter;

		if (! nih_strcat_sprintf (&code_block, NULL,
					  ", &%s",
					  var->name))
			return NULL;

		nih_list_add (&locals, &var->entry);

		/* Handler argument is pointer */
		arg = type_var_new (handler_func, var->type, var->name);
		if (! arg)
			return NULL;

		if (! type_to_pointer (&arg->type, arg))
			return NULL;

		nih_list_add (&handler_func->args, &arg->entry);
	}

	/* Finish up the calling block, in case of error we again just
	 * return and let our caller deal with it.
	 */
	if (! nih_strcat_sprintf (&code_block, NULL, ") < 0)\n"
				  "\treturn -1;\n"
				  "\n"))
		return NULL;

	/* Surround the marshalling code by appending a variant onto the
	 * passed-in message iterator, and closing it once complete.
	 */
	if (! nih_strcat_sprintf (&code_block, NULL,
				  "/* Append a variant onto the message to contain the property value. */\n"
				  "if (! dbus_message_iter_open_container (iter, DBUS_TYPE_VARIANT, \"%s\", &variter))\n"
				  "\treturn -1;\n"
				  "\n"
				  "%s"
				  "\n"
				  "/* Finish the variant */\n"
				  "if (! dbus_message_iter_close_container (iter, &variter))\n"
				  "\treturn -1;\n",
				  property->type,
				  block))
		return NULL;

	/* Lay out the function body, indenting it all before placing it
	 * in the function code.
	 */
	vars_block = type_var_layout (NULL, &locals);
	if (! vars_block)
		return NULL;

	if (! nih_strcat_sprintf (&body, NULL,
				  "%s"
				  "\n"
				  "nih_assert (object != NULL);\n"
				  "nih_assert (message != NULL);\n"
				  "nih_assert (iter != NULL);\n"
				  "\n"
				  "%s"
				  "\n"
				  "return 0;\n",
				  vars_block,
				  code_block))
		return NULL;

	if (! indent (&body, NULL, 1))
		return NULL;

	/* Function header */
	code = type_func_to_string (parent, func);
	if (! code)
		return NULL;

	if (! nih_strcat_sprintf (&code, parent,
				  "{\n"
				  "%s"
				  "}\n",
				  body)) {
		nih_free (code);
		return NULL;
	}

	/* Append the functions to the prototypes and handlers lists */
	nih_list_add (prototypes, &func->entry);
	nih_ref (func, code);

	nih_list_add (handlers, &handler_func->entry);
	nih_ref (handler_func, code);

	return code;
}

/**
 * property_object_set_function:
 * @parent: parent object for new string,
 * @property: property to generate function for,
 * @name: name of function to generate,
 * @handler_name: name of handler function to call,
 * @prototypes: list to append function prototypes to,
 * @handlers: list to append definitions of required handlers to.
 *
 * Generates C code for a function called @name that will extract the new
 * value of a property @property from a variant at the D-Bus message iterator
 * passed.  The new value of the property is then passed to a function named
 * @handler_name to set it, the prototype for this function is specified as
 * a TypeFunc object added to the @handlers list.
 *
 * The prototype of the function is given as a TypeFunc object appended to
 * the @prototypes list, with the name as @name itself.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are freed, the return string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
char *
property_object_set_function (const void *parent,
			      Property *  property,
			      const char *name,
			      const char *handler_name,
			      NihList *   prototypes,
			      NihList *   handlers)
{
	DBusSignatureIter   iter;
	NihList             outputs;
	NihList             locals;
	nih_local TypeFunc *func = NULL;
	TypeVar *           arg;
	nih_local TypeVar * iter_var = NULL;
	nih_local TypeVar * reply_var = NULL;
	nih_local char *    demarshal_block = NULL;
	nih_local char *    oom_error_code = NULL;
	nih_local char *    type_error_code = NULL;
	nih_local char *    block = NULL;
	nih_local char *    call_block = NULL;
	nih_local TypeFunc *handler_func = NULL;
	NihListEntry *      attrib;
	nih_local char *    vars_block = NULL;
	nih_local char *    body = NULL;
	char *              code;

	nih_assert (property != NULL);
	nih_assert (name != NULL);
	nih_assert (handler_name != NULL);
	nih_assert (prototypes != NULL);
	nih_assert (handlers != NULL);

	dbus_signature_iter_init (&iter, property->type);

	nih_list_init (&outputs);
	nih_list_init (&locals);

	/* The function returns an integer, which means success when zero
	 * or a raised error when non-zero and accepts arguments for the
	 * D-Bus object, message and a message iterator.
	 */
	func = type_func_new (NULL, "int", name);
	if (! func)
		return NULL;

	arg = type_var_new (func, "NihDBusObject *", "object");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	arg = type_var_new (func, "NihDBusMessage *", "message");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	arg = type_var_new (func, "DBusMessageIter *", "iter");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	/* The function requires a local iterator for the variant.  Rather
	 * than deal with this by hand, it's far easier to put it on the
	 * locals list and deal with them along with the rest.
	 */
	iter_var = type_var_new (NULL, "DBusMessageIter", "variter");
	if (! iter_var)
		return NULL;

	nih_list_add (&locals, &iter_var->entry);

	/* Make sure that the iterator points to a variant, then open the
	 * variant.
	 */
	if (! nih_strcat_sprintf (&demarshal_block, NULL,
				  "/* Recurse into the variant */\n"
				  "if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_VARIANT) {\n"
				  "\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				  "\t                             _(\"Invalid arguments to %s property\"));\n"
				  "\treturn -1;\n"
				  "}\n"
				  "\n"
				  "dbus_message_iter_recurse (iter, &variter);\n"
				  "\n",
				  property->name))
		return NULL;

	/* In case of out of memory, or type error, return a raised error
	 * to the caller.
	 */
	oom_error_code = nih_strdup (NULL,
				     "nih_error_raise_no_memory ();\n"
				     "return -1;\n");
	if (! oom_error_code)
		return NULL;

	type_error_code = nih_sprintf (NULL,
				       "nih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				       "                             _(\"Invalid arguments to %s property\"));\n"
				       "return -1;\n",
				       property->name);
	if (! type_error_code)
		return NULL;

	block = demarshal (NULL, &iter, "message", "variter", "value",
			   oom_error_code,
			   type_error_code,
			   &outputs, &locals);
	if (! block)
		return NULL;

	/* Complete the demarshalling block, checking for any unexpected
	 * arguments which we also want to error on and begin the handler
	 * calling block.
	 */
	if (! nih_strcat_sprintf (&call_block, NULL,
				  "dbus_message_iter_next (iter);\n"
				  "\n"
				  "if (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_INVALID) {\n"
				  "\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				  "\t                             _(\"Invalid arguments to %s property\"));\n"
				  "\treturn -1;\n"
				  "}\n"
				  "\n"
				  "/* Call the handler function */\n"
				  "if (%s (object->data, message",
				  property->name,
				  handler_name))
		return NULL;

	handler_func = type_func_new (NULL, "int", handler_name);
	if (! handler_func)
		return NULL;

	attrib = nih_list_entry_new (handler_func);
	if (! attrib)
		return NULL;

	attrib->str = nih_strdup (attrib, "warn_unused_result");
	if (! attrib->str)
		return NULL;

	nih_list_add (&handler_func->attribs, &attrib->entry);

	arg = type_var_new (handler_func, "void *", "data");
	if (! arg)
		return NULL;

	nih_list_add (&handler_func->args, &arg->entry);

	arg = type_var_new (handler_func, "NihDBusMessage *", "message");
	if (! arg)
		return NULL;

	nih_list_add (&handler_func->args, &arg->entry);

	/* Each of the outputs from the demarshalling code becomes a local
	 * variable to our function that we pass to the implementation
	 * function.
	 */
	NIH_LIST_FOREACH_SAFE (&outputs, iter) {
		TypeVar *var = (TypeVar *)iter;

		if (! nih_strcat_sprintf (&call_block, NULL,
					  ", %s",
					  var->name))
			return NULL;

		nih_list_add (&locals, &var->entry);

		/* Handler argument is const */
		arg = type_var_new (handler_func, var->type, var->name);
		if (! arg)
			return NULL;

		if (! type_to_const (&arg->type, arg))
			return NULL;

		nih_list_add (&handler_func->args, &arg->entry);
	}

	/* Finish up the calling block, in case of out of memory error we
	 * return and let D-Bus deal with it, other errors generate an
	 * error reply.
	 */
	if (! nih_strcat_sprintf (&call_block, NULL, ") < 0)\n"
				  "\treturn -1;\n"))
		return NULL;

	/* Lay out the function body, indenting it all before placing it
	 * in the function code.
	 */
	vars_block = type_var_layout (NULL, &locals);
	if (! vars_block)
		return NULL;

	if (! nih_strcat_sprintf (&body, NULL,
				  "%s"
				  "\n"
				  "nih_assert (object != NULL);\n"
				  "nih_assert (message != NULL);\n"
				  "nih_assert (iter != NULL);\n"
				  "\n"
				  "%s"
				  "%s"
				  "\n"
				  "%s"
				  "\n"
				  "return 0;\n",
				  vars_block,
				  demarshal_block,
				  block,
				  call_block))
		return NULL;

	if (! indent (&body, NULL, 1))
		return NULL;

	/* Function header */
	code = type_func_to_string (parent, func);
	if (! code)
		return NULL;

	if (! nih_strcat_sprintf (&code, parent,
				  "{\n"
				  "%s"
				  "}\n",
				  body)) {
		nih_free (code);
		return NULL;
	}

	/* Append the functions to the prototypes and handlers lists */
	nih_list_add (prototypes, &func->entry);
	nih_ref (func, code);

	nih_list_add (handlers, &handler_func->entry);
	nih_ref (handler_func, code);

	return code;
}


/**
 * property_proxy_get_sync_function:
 * @parent: parent object for new string.
 * @interface_name: name of interface,
 * @property: property to generate function for,
 * @name: name of function to generate,
 * @prototypes: list to append function prototypes to.
 *
 * Generates C code for a function called @name that will make a
 * synchronous method call to obtain the value of the property @property.
 * The interface name of the property must be supplied in @interface_name.
 *
 * The prototype of the function is given as a TypeFunc object appended to
 * the @prototypes list, with the name as @name itself.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are freed, the return string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
char *
property_proxy_get_sync_function (const void *parent,
				  const char *interface_name,
				  Property *  property,
				  const char *name,
				  NihList *   prototypes)
{
	DBusSignatureIter   iter;
	NihList             outputs;
	NihList             locals;
	nih_local TypeFunc *func = NULL;
	TypeVar *           arg;
	NihListEntry *      attrib;
	nih_local char *    assert_block = NULL;
	nih_local TypeVar * message_var = NULL;
	nih_local TypeVar * iter_var = NULL;
	nih_local TypeVar * variter_var = NULL;
	nih_local TypeVar * error_var = NULL;
	nih_local TypeVar * reply_var = NULL;
	nih_local TypeVar * interface_var = NULL;
	nih_local TypeVar * property_var = NULL;
	nih_local char *    call_block = NULL;
	nih_local char *    demarshal_block = NULL;
	nih_local char *    oom_error_code = NULL;
	nih_local char *    type_error_code = NULL;
	nih_local char *    block = NULL;
	nih_local char *    vars_block = NULL;
	nih_local char *    body = NULL;
	char *              code = NULL;

	nih_assert (interface_name != NULL);
	nih_assert (property != NULL);
	nih_assert (name != NULL);
	nih_assert (prototypes != NULL);

	dbus_signature_iter_init (&iter, property->type);

	nih_list_init (&outputs);
	nih_list_init (&locals);

	/* The function returns an integer, and takes a parent object and
	 * the proxy object as the argument along with an output argument
	 * for the property value.  The integer is negative if a raised
	 * error occurred, so we want warning if the result isn't used.
	 * Since this is used by the client, we also add a deprecated
	 * attribute if the property is deprecated.
	 */
	func = type_func_new (NULL, "int", name);
	if (! func)
		return NULL;

	attrib = nih_list_entry_new (func);
	if (! attrib)
		return NULL;

	attrib->str = nih_strdup (attrib, "warn_unused_result");
	if (! attrib->str)
		return NULL;

	nih_list_add (&func->attribs, &attrib->entry);

	if (property->deprecated) {
		attrib = nih_list_entry_new (func);
		if (! attrib)
			return NULL;

		attrib->str = nih_strdup (attrib, "deprecated");
		if (! attrib->str)
			return NULL;

		nih_list_add (&func->attribs, &attrib->entry);
	}

	arg = type_var_new (func, "const void *", "parent");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	arg = type_var_new (func, "NihDBusProxy *", "proxy");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	if (! nih_strcat (&assert_block, NULL,
			  "nih_assert (proxy != NULL);\n"))
		return NULL;


	/* The function requires a message pointer, which we allocate,
	 * and an iterator for it to append the arguments.  We also need
	 * a reply message pointer as well and an error object.
	 * Rather than deal with these by hand, it's far easier to put them
	 * on the locals list and deal with them along with the rest.
	 */
	message_var = type_var_new (NULL, "DBusMessage *", "method_call");
	if (! message_var)
		return NULL;

	nih_list_add (&locals, &message_var->entry);

	iter_var = type_var_new (NULL, "DBusMessageIter", "iter");
	if (! iter_var)
		return NULL;

	nih_list_add (&locals, &iter_var->entry);

	variter_var = type_var_new (NULL, "DBusMessageIter", "variter");
	if (! variter_var)
		return NULL;

	nih_list_add (&locals, &variter_var->entry);

	error_var = type_var_new (NULL, "DBusError", "error");
	if (! error_var)
		return NULL;

	nih_list_add (&locals, &error_var->entry);

	reply_var = type_var_new (NULL, "DBusMessage *", "reply");
	if (! reply_var)
		return NULL;

	nih_list_add (&locals, &reply_var->entry);

	/* Annoyingly we also need variables for the interface and
	 * property names, since D-Bus wants their address and can't just
	 * take a constant string.
	 */
	interface_var = type_var_new (NULL, "const char *", "interface");
	if (! interface_var)
		return NULL;

	nih_list_add (&locals, &interface_var->entry);

	property_var = type_var_new (NULL, "const char *", "property");
	if (! property_var)
		return NULL;

	nih_list_add (&locals, &property_var->entry);


	/* Create the method call to get the property, the property
	 * interface gets specified as an argument - the method call
	 * interface is the D-Bus properties one.
	 */
	if (! nih_strcat_sprintf (&call_block, NULL,
				  "/* Construct the method call message. */\n"
				  "method_call = dbus_message_new_method_call (proxy->name, proxy->path, \"%s\", \"Get\");\n"
				  "if (! method_call)\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "\n"
				  "dbus_message_iter_init_append (method_call, &iter);\n"
				  "\n"
				  "interface = \"%s\";\n"
				  "if (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &interface))\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "\n"
				  "property = \"%s\";\n"
				  "if (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &property))\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "\n",
				  DBUS_INTERFACE_PROPERTIES,
				  interface_name,
				  property->name))
		return NULL;

	/* FIXME autostart? */

	/* Complete the marshalling block by sending the message and checking
	 * for error replies.
	 */
	if (! nih_strcat_sprintf (&call_block, NULL,
				  "/* Send the message, and wait for the reply. */\n"
				  "dbus_error_init (&error);\n"
				  "\n"
				  "reply = dbus_connection_send_with_reply_and_block (proxy->conn, method_call, -1, &error);\n"
				  "if (! reply) {\n"
				  "\tdbus_message_unref (method_call);\n"
				  "\n"
				  "\tif (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY)) {\n"
				  "\t\tnih_error_raise_no_memory ();\n"
				  "\t} else {\n"
				  "\t\tnih_dbus_error_raise (error.name, error.message);\n"
				  "\t}\n"
				  "\n"
				  "\tdbus_error_free (&error);\n"
				  "\treturn -1;\n"
				  "}\n"
				  "\n"))
		return NULL;

	/* Begin the demarshalling block, making sure the first argument
	 * is a variant and recursing into it and also making sure that
	 * there are no subsequent arguments before we allocate the
	 * return value.
	 */
	if (! nih_strcat_sprintf (&demarshal_block, NULL,
				  "dbus_message_unref (method_call);\n"
				  "\n"
				  "/* Iterate the method arguments, recursing into the variant */\n"
				  "dbus_message_iter_init (reply, &iter);\n"
				  "\n"
				  "if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_VARIANT) {\n"
				  "\tdbus_message_unref (reply);\n"
				  "\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				  "\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				  "}\n"
				  "\n"
				  "dbus_message_iter_recurse (&iter, &variter);\n"
				  "\n"
				  "dbus_message_iter_next (&iter);\n"
				  "\n"
				  "if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID) {\n"
				  "\tdbus_message_unref (reply);\n"
				  "\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				  "\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				  "}\n"
				  "\n"))
		return NULL;

	/* In case of out of memory, we can't just return because we've
	 * already made the method call so we loop over the code instead.
	 * But in case of type error in the returned arguments, all we
	 * can do is return an error.
	 */
	oom_error_code = nih_sprintf (NULL,
				      "*value = NULL;\n"
				      "goto enomem;\n");
	if (! oom_error_code)
		return NULL;

	type_error_code = nih_strdup (NULL,
				      "dbus_message_unref (reply);\n"
				      "nih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				      "                  _(NIH_DBUS_INVALID_ARGS_STR));\n");
	if (! type_error_code)
		return NULL;

	block = demarshal (NULL, &iter, "parent", "variter", "local",
			   oom_error_code,
			   type_error_code,
			   &outputs, &locals);
	if (! block)
		return NULL;

	if (! nih_strcat (&block, NULL, "\n"))
		return NULL;

	/* Each of the outputs from the demarshalling code becomes a local
	 * variable to our function that we store the value in, and an
	 * argument to the function that we set when done.
	 */
	NIH_LIST_FOREACH_SAFE (&outputs, iter) {
		TypeVar *       var = (TypeVar *)iter;
		nih_local char *arg_type = NULL;
		const char *    suffix;
		nih_local char *arg_name = NULL;
		TypeVar *       arg;

		/* Output variable */
		arg_type = nih_strdup (NULL, var->type);
		if (! arg_type)
			return NULL;

		if (! type_to_pointer (&arg_type, NULL))
			return NULL;

		nih_assert (! strncmp (var->name, "local", 5));
		suffix = var->name + 5;

		arg_name = nih_sprintf (NULL, "value%s", suffix);
		if (! arg_name)
			return NULL;

		arg = type_var_new (func, arg_type, arg_name);
		if (! arg)
			return NULL;

		nih_list_add (&func->args, &arg->entry);

		if (! nih_strcat_sprintf (&assert_block, NULL,
					  "nih_assert (%s != NULL);\n",
					  arg->name))
			return NULL;

		/* Copy from local variable to output */
		if (! nih_strcat_sprintf (&block, NULL,
					  "*%s = %s;\n",
					  arg->name, var->name))
			return NULL;

		nih_list_add (&locals, &var->entry);
		nih_ref (var, demarshal_block);
	}

	/* Loop over the demarshalling code for out-of-memory situations */
	if (! indent (&block, NULL, 1))
		return NULL;

	if (! nih_strcat_sprintf (&demarshal_block, NULL,
				  "do {\n"
				  "\t__label__ enomem;\n"
				  "\n"
				  "%s"
				  "enomem: __attribute__ ((unused));\n"
				  "} while (! *value);\n"
				  "\n"
				  "dbus_message_unref (reply);\n",
				  block))
		return NULL;

	/* Lay out the function body, indenting it all before placing it
	 * in the function code.
	 */
	vars_block = type_var_layout (NULL, &locals);
	if (! vars_block)
		return NULL;

	if (! nih_strcat_sprintf (&body, NULL,
				  "%s"
				  "\n"
				  "%s"
				  "\n"
				  "%s"
				  "%s"
				  "\n"
				  "return 0;\n",
				  vars_block,
				  assert_block,
				  call_block,
				  demarshal_block))
		return NULL;

	if (! indent (&body, NULL, 1))
		return NULL;

	/* Function header */
	code = type_func_to_string (parent, func);
	if (! code)
		return NULL;

	if (! nih_strcat_sprintf (&code, parent,
				  "{\n"
				  "%s"
				  "}\n",
				  body)) {
		nih_free (code);
		return NULL;
	}

	/* Append the function to the prototypes list */
	nih_list_add (prototypes, &func->entry);
	nih_ref (func, code);

	return code;
}

/**
 * property_proxy_set_sync_function:
 * @parent: parent object for new string.
 * @interface_name: name of interface,
 * @property: property to generate function for,
 * @name: name of function to generate,
 * @prototypes: list to append function prototypes to.
 *
 * Generates C code for a function called @name that will make a
 * synchronous method call to set the value of the property @property.
 * The interface name of the property must be supplied in @interface_name.
 *
 * The prototype of the function is given as a TypeFunc object appended to
 * the @prototypes list, with the name as @name itself.
 *
 * If @parent is not NULL, it should be a pointer to another object which
 * will be used as a parent for the returned string.  When all parents
 * of the returned string are freed, the return string will also be
 * freed.
 *
 * Returns: newly allocated string or NULL if insufficient memory.
 **/
char *
property_proxy_set_sync_function (const void *parent,
				  const char *interface_name,
				  Property *  property,
				  const char *name,
				  NihList *   prototypes)
{
	DBusSignatureIter   iter;
	NihList             inputs;
	NihList             locals;
	nih_local TypeFunc *func = NULL;
	TypeVar *           arg;
	NihListEntry *      attrib;
	nih_local char *    assert_block = NULL;
	nih_local TypeVar * message_var = NULL;
	nih_local TypeVar * iter_var = NULL;
	nih_local TypeVar * variter_var = NULL;
	nih_local TypeVar * error_var = NULL;
	nih_local TypeVar * reply_var = NULL;
	nih_local TypeVar * interface_var = NULL;
	nih_local TypeVar * property_var = NULL;
	nih_local char *    marshal_block = NULL;
	nih_local char *    call_block = NULL;
	nih_local char *    oom_error_code = NULL;
	nih_local char *    block = NULL;
	nih_local char *    vars_block = NULL;
	nih_local char *    body = NULL;
	char *              code = NULL;

	nih_assert (interface_name != NULL);
	nih_assert (property != NULL);
	nih_assert (name != NULL);
	nih_assert (prototypes != NULL);

	dbus_signature_iter_init (&iter, property->type);

	nih_list_init (&inputs);
	nih_list_init (&locals);

	/* The function returns an integer, and takes the proxy object
	 * as the argument along with an input argument for the property
	 * value.  The integer is negative if a raised error occurred,
	 * so we want warning if the result isn't used.  Since this is
	 * used by the client, we also add a deprecated attribute if
	 * the property is deprecated.
	 */
	func = type_func_new (NULL, "int", name);
	if (! func)
		return NULL;

	attrib = nih_list_entry_new (func);
	if (! attrib)
		return NULL;

	attrib->str = nih_strdup (attrib, "warn_unused_result");
	if (! attrib->str)
		return NULL;

	nih_list_add (&func->attribs, &attrib->entry);

	if (property->deprecated) {
		attrib = nih_list_entry_new (func);
		if (! attrib)
			return NULL;

		attrib->str = nih_strdup (attrib, "deprecated");
		if (! attrib->str)
			return NULL;

		nih_list_add (&func->attribs, &attrib->entry);
	}

	arg = type_var_new (func, "NihDBusProxy *", "proxy");
	if (! arg)
		return NULL;

	nih_list_add (&func->args, &arg->entry);

	if (! nih_strcat (&assert_block, NULL,
			  "nih_assert (proxy != NULL);\n"))
		return NULL;


	/* The function requires a message pointer, which we allocate,
	 * and an iterator for it to append the arguments.  We also need
	 * a reply message pointer as well and an error object.
	 * Rather than deal with these by hand, it's far easier to put them
	 * on the locals list and deal with them along with the rest.
	 */
	message_var = type_var_new (NULL, "DBusMessage *", "method_call");
	if (! message_var)
		return NULL;

	nih_list_add (&locals, &message_var->entry);

	iter_var = type_var_new (NULL, "DBusMessageIter", "iter");
	if (! iter_var)
		return NULL;

	nih_list_add (&locals, &iter_var->entry);

	variter_var = type_var_new (NULL, "DBusMessageIter", "variter");
	if (! variter_var)
		return NULL;

	nih_list_add (&locals, &variter_var->entry);

	error_var = type_var_new (NULL, "DBusError", "error");
	if (! error_var)
		return NULL;

	nih_list_add (&locals, &error_var->entry);

	reply_var = type_var_new (NULL, "DBusMessage *", "reply");
	if (! reply_var)
		return NULL;

	nih_list_add (&locals, &reply_var->entry);

	/* Annoyingly we also need variables for the interface and
	 * property names, since D-Bus wants their address and can't just
	 * take a constant string.
	 */
	interface_var = type_var_new (NULL, "const char *", "interface");
	if (! interface_var)
		return NULL;

	nih_list_add (&locals, &interface_var->entry);

	property_var = type_var_new (NULL, "const char *", "property");
	if (! property_var)
		return NULL;

	nih_list_add (&locals, &property_var->entry);


	/* Create the method call to set the property, the property
	 * interface gets specified as an argument - the method call
	 * interface is the D-Bus properties one.  Append a variant
	 * which is where we put the new value.
	 */
	if (! nih_strcat_sprintf (&marshal_block, NULL,
				  "/* Construct the method call message. */\n"
				  "method_call = dbus_message_new_method_call (proxy->name, proxy->path, \"%s\", \"Set\");\n"
				  "if (! method_call)\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "\n"
				  "dbus_message_iter_init_append (method_call, &iter);\n"
				  "\n"
				  "interface = \"%s\";\n"
				  "if (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &interface))\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "\n"
				  "property = \"%s\";\n"
				  "if (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &property))\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "\n"
				  "if (! dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT, \"%s\", &variter))\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "\n",
				  DBUS_INTERFACE_PROPERTIES,
				  interface_name,
				  property->name,
				  property->type))
		return NULL;

	/* FIXME autostart? */

	/* In case of out of memory, we just return the error to the caller
	 * since we haven't made the method call yet.
	 */
	oom_error_code = nih_sprintf (NULL,
				      "dbus_message_iter_close_container (&iter, &variter);\n"
				      "dbus_message_unref (method_call);\n"
				      "nih_return_no_memory_error (-1);\n");
	if (! oom_error_code)
		return NULL;

	block = marshal (NULL, &iter, "variter", "value",
			 oom_error_code,
			 &inputs, &locals);
	if (! block)
		return NULL;

	if (! nih_strcat_sprintf (&marshal_block, NULL,
				  "%s"
				  "\n",
				  block))
		return NULL;

	/* Each of the inputs of the marshalling code becomes a const
	 * argument to our function that we obtain the value from.
	 */
	NIH_LIST_FOREACH_SAFE (&inputs, iter) {
		TypeVar *var = (TypeVar *)iter;

		if (! type_to_const (&var->type, var))
			return NULL;

		if (strchr (var->type, '*'))
			if (! nih_strcat_sprintf (&assert_block, NULL,
						  "nih_assert (%s != NULL);\n",
						  var->name))
				return NULL;

		nih_list_add (&func->args, &var->entry);
		nih_ref (var, func);
	}

	/* Complete the marshalling block by closing the container. */
	if (! nih_strcat_sprintf (&marshal_block, NULL,
				  "if (! dbus_message_iter_close_container (&iter, &variter)) {\n"
				  "\tdbus_message_unref (method_call);\n"
				  "\tnih_return_no_memory_error (-1);\n"
				  "}\n"
				  "\n"))
		return NULL;

	/* Send the message and check for error replies, or arguments
	 * in the reply (which is an error).
	 */
	if (! nih_strcat_sprintf (&call_block, NULL,
				  "/* Send the message, and wait for the reply. */\n"
				  "dbus_error_init (&error);\n"
				  "\n"
				  "reply = dbus_connection_send_with_reply_and_block (proxy->conn, method_call, -1, &error);\n"
				  "if (! reply) {\n"
				  "\tdbus_message_unref (method_call);\n"
				  "\n"
				  "\tif (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY)) {\n"
				  "\t\tnih_error_raise_no_memory ();\n"
				  "\t} else {\n"
				  "\t\tnih_dbus_error_raise (error.name, error.message);\n"
				  "\t}\n"
				  "\n"
				  "\tdbus_error_free (&error);\n"
				  "\treturn -1;\n"
				  "}\n"
				  "\n"
				  "/* Check the reply has no arguments */\n"
				  "dbus_message_unref (method_call);\n"
				  "dbus_message_iter_init (reply, &iter);\n"
				  "\n"
				  "if (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID) {\n"
				  "\tdbus_message_unref (reply);\n"
				  "\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				  "\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				  "}\n"
				  "\n"
				  "dbus_message_unref (reply);\n"))
		return NULL;

	/* Lay out the function body, indenting it all before placing it
	 * in the function code.
	 */
	vars_block = type_var_layout (NULL, &locals);
	if (! vars_block)
		return NULL;

	if (! nih_strcat_sprintf (&body, NULL,
				  "%s"
				  "\n"
				  "%s"
				  "\n"
				  "%s"
				  "%s"
				  "\n"
				  "return 0;\n",
				  vars_block,
				  assert_block,
				  marshal_block,
				  call_block))
		return NULL;

	if (! indent (&body, NULL, 1))
		return NULL;

	/* Function header */
	code = type_func_to_string (parent, func);
	if (! code)
		return NULL;

	if (! nih_strcat_sprintf (&code, parent,
				  "{\n"
				  "%s"
				  "}\n",
				  body)) {
		nih_free (code);
		return NULL;
	}

	/* Append the function to the prototypes list */
	nih_list_add (prototypes, &func->entry);
	nih_ref (func, code);

	return code;
}
