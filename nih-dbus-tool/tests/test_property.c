/* nih-dbus-tool
 *
 * test_property.c - test suite for nih-dbus-tool/property.c
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

#include <expat.h>

#include <errno.h>
#include <assert.h>
#include <string.h>

#include <nih/macros.h>
#include <nih/alloc.h>
#include <nih/list.h>
#include <nih/string.h>
#include <nih/main.h>
#include <nih/error.h>

#include <nih-dbus/dbus_error.h>
#include <nih-dbus/dbus_message.h>
#include <nih-dbus/errors.h>

#include "type.h"
#include "node.h"
#include "property.h"
#include "parse.h"
#include "errors.h"
#include "tests/property_code.h"


void
test_name_valid (void)
{
	TEST_FUNCTION ("property_name_valid");

	/* Check that a typical property name is valid. */
	TEST_FEATURE ("with typical property name");
	TEST_TRUE (property_name_valid ("Wibble"));


	/* Check that an property name is not valid if it is has an
	 * initial period.
	 */
	TEST_FEATURE ("with initial period");
	TEST_FALSE (property_name_valid (".Wibble"));


	/* Check that an property name is not valid if it ends with a period
	 */
	TEST_FEATURE ("with final period");
	TEST_FALSE (property_name_valid ("Wibble."));


	/* Check that an property name is not valid if it contains a period
	 */
	TEST_FEATURE ("with period");
	TEST_FALSE (property_name_valid ("Wib.ble"));


	/* Check that a property name may contain numbers */
	TEST_FEATURE ("with numbers");
	TEST_TRUE (property_name_valid ("Wib43ble"));


	/* Check that a property name may not begin with numbers */
	TEST_FEATURE ("with leading digits");
	TEST_FALSE (property_name_valid ("43Wibble"));


	/* Check that a property name may end with numbers */
	TEST_FEATURE ("with trailing digits");
	TEST_TRUE (property_name_valid ("Wibble43"));


	/* Check that a property name may contain underscores */
	TEST_FEATURE ("with underscore");
	TEST_TRUE (property_name_valid ("Wib_ble"));


	/* Check that a property name may begin with underscores */
	TEST_FEATURE ("with initial underscore");
	TEST_TRUE (property_name_valid ("_Wibble"));


	/* Check that a property name may end with underscores */
	TEST_FEATURE ("with final underscore");
	TEST_TRUE (property_name_valid ("Wibble_"));


	/* Check that other characters are not permitted */
	TEST_FEATURE ("with non-permitted characters");
	TEST_FALSE (property_name_valid ("Wib-ble"));


	/* Check that an empty property name is invalid */
	TEST_FEATURE ("with empty string");
	TEST_FALSE (property_name_valid (""));


	/* Check that an property name may not exceed 255 characters */
	TEST_FEATURE ("with overly long name");
	TEST_FALSE (property_name_valid ("ReallyLongPropertyNameThatNobo"
					 "dyInTheirRightMindWouldEverUse"
					 "NotInTheLeastBecauseThenYoudEn"
					 "dUpWithAnEvenLongerInterfaceNa"
					 "meAndThatJustWontWorkWhenCombi"
					 "nedButStillWeTestThisShitJustI"
					 "ncaseSomeoneTriesItBecauseThat"
					 "sWhatTestDrivenDevelopmentIsAl"
					 "lAboutYayDoneNow"));
}


void
test_new (void)
{
	Property *property;

	/* Check that an Property object is allocated with the structure
	 * filled in properly, but not placed in a list.
	 */
	TEST_FUNCTION ("property_new");
	TEST_ALLOC_FAIL {
		property = property_new (NULL, "Size", "i", NIH_DBUS_READ);

		if (test_alloc_failed) {
			TEST_EQ_P (property, NULL);
			continue;
		}

		TEST_ALLOC_SIZE (property, sizeof (Property));
		TEST_LIST_EMPTY (&property->entry);
		TEST_EQ_STR (property->name, "Size");
		TEST_ALLOC_PARENT (property->name, property);
		TEST_EQ_STR (property->type, "i");
		TEST_ALLOC_PARENT (property->type, property);
		TEST_EQ_P (property->symbol, NULL);
		TEST_EQ (property->access, NIH_DBUS_READ);
		TEST_FALSE (property->deprecated);

		nih_free (property);
	}
}


void
test_start_tag (void)
{
	ParseContext context;
	ParseStack * parent = NULL;
	ParseStack * entry;
	XML_Parser   xmlp;
	Interface *  interface = NULL;
	Property *   property;
	char *       attr[9];
	int          ret;
	NihError *   err;
	FILE *       output;

	TEST_FUNCTION ("property_start_tag");
	context.parent = NULL;
	nih_list_init (&context.stack);
	context.filename = "foo";
	context.node = NULL;

	assert (xmlp = XML_ParserCreate ("UTF-8"));
	XML_SetUserData (xmlp, &context);

	output = tmpfile ();


	/* Check that an property tag for an interface with the usual name,
	 * and type attributes and with an access attribute of read results
	 * in an Property member being created and pushed onto the stack
	 * with the attributes filled in correctly for a read-only property.
	 */
	TEST_FEATURE ("with read-only property");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			interface = interface_new (NULL, "com.netsplit.Nih.Test");
			parent = parse_stack_push (NULL, &context.stack,
						   PARSE_INTERFACE, interface);
		}

		attr[0] = "name";
		attr[1] = "TestProperty";
		attr[2] = "type";
		attr[3] = "s";
		attr[4] = "access";
		attr[5] = "read";
		attr[6] = NULL;

		ret = property_start_tag (xmlp, "property", attr);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			TEST_EQ_P (parse_stack_top (&context.stack),
				   parent);

			TEST_LIST_EMPTY (&interface->properties);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (parent);
			continue;
		}

		TEST_EQ (ret, 0);

		entry = parse_stack_top (&context.stack);
		TEST_NE_P (entry, parent);
		TEST_ALLOC_SIZE (entry, sizeof (ParseStack));
		TEST_EQ (entry->type, PARSE_PROPERTY);

		property = entry->property;
		TEST_ALLOC_SIZE (property, sizeof (Property));
		TEST_ALLOC_PARENT (property, entry);
		TEST_EQ_STR (property->name, "TestProperty");
		TEST_ALLOC_PARENT (property->name, property);
		TEST_EQ_P (property->symbol, NULL);
		TEST_EQ_STR (property->type, "s");
		TEST_ALLOC_PARENT (property->type, property);
		TEST_EQ (property->access, NIH_DBUS_READ);

		TEST_LIST_EMPTY (&interface->properties);

		nih_free (entry);
		nih_free (parent);
	}


	/* Check that an property tag for an interface with the usual name,
	 * and type attributes and with an access attribute of write results
	 * in an Property member being created and pushed onto the stack
	 * with the attributes filled in correctly for a write-only property.
	 */
	TEST_FEATURE ("with write-only property");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			interface = interface_new (NULL, "com.netsplit.Nih.Test");
			parent = parse_stack_push (NULL, &context.stack,
						   PARSE_INTERFACE, interface);
		}

		attr[0] = "name";
		attr[1] = "TestProperty";
		attr[2] = "type";
		attr[3] = "s";
		attr[4] = "access";
		attr[5] = "write";
		attr[6] = NULL;

		ret = property_start_tag (xmlp, "property", attr);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			TEST_EQ_P (parse_stack_top (&context.stack),
				   parent);

			TEST_LIST_EMPTY (&interface->properties);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (parent);
			continue;
		}

		TEST_EQ (ret, 0);

		entry = parse_stack_top (&context.stack);
		TEST_NE_P (entry, parent);
		TEST_ALLOC_SIZE (entry, sizeof (ParseStack));
		TEST_EQ (entry->type, PARSE_PROPERTY);

		property = entry->property;
		TEST_ALLOC_SIZE (property, sizeof (Property));
		TEST_ALLOC_PARENT (property, entry);
		TEST_EQ_STR (property->name, "TestProperty");
		TEST_ALLOC_PARENT (property->name, property);
		TEST_EQ_P (property->symbol, NULL);
		TEST_EQ_STR (property->type, "s");
		TEST_ALLOC_PARENT (property->type, property);
		TEST_EQ (property->access, NIH_DBUS_WRITE);

		TEST_LIST_EMPTY (&interface->properties);

		nih_free (entry);
		nih_free (parent);
	}


	/* Check that an property tag for an interface with the usual name,
	 * and type attributes and with an access attribute of readwrite
	 * results in an Property member being created and pushed onto the
	 * stack with the attributes filled in correctly for a read/write
	 * property.
	 */
	TEST_FEATURE ("with read/write property");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			interface = interface_new (NULL, "com.netsplit.Nih.Test");
			parent = parse_stack_push (NULL, &context.stack,
						   PARSE_INTERFACE, interface);
		}

		attr[0] = "name";
		attr[1] = "TestProperty";
		attr[2] = "type";
		attr[3] = "s";
		attr[4] = "access";
		attr[5] = "readwrite";
		attr[6] = NULL;

		ret = property_start_tag (xmlp, "property", attr);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			TEST_EQ_P (parse_stack_top (&context.stack),
				   parent);

			TEST_LIST_EMPTY (&interface->properties);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (parent);
			continue;
		}

		TEST_EQ (ret, 0);

		entry = parse_stack_top (&context.stack);
		TEST_NE_P (entry, parent);
		TEST_ALLOC_SIZE (entry, sizeof (ParseStack));
		TEST_EQ (entry->type, PARSE_PROPERTY);

		property = entry->property;
		TEST_ALLOC_SIZE (property, sizeof (Property));
		TEST_ALLOC_PARENT (property, entry);
		TEST_EQ_STR (property->name, "TestProperty");
		TEST_ALLOC_PARENT (property->name, property);
		TEST_EQ_P (property->symbol, NULL);
		TEST_EQ_STR (property->type, "s");
		TEST_ALLOC_PARENT (property->type, property);
		TEST_EQ (property->access, NIH_DBUS_READWRITE);

		TEST_LIST_EMPTY (&interface->properties);

		nih_free (entry);
		nih_free (parent);
	}


	/* Check that a property with a missing name attribute results
	 * in an error being raised.
	 */
	TEST_FEATURE ("with missing name");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			interface = interface_new (NULL, "com.netsplit.Nih.Test");
			parent = parse_stack_push (NULL, &context.stack,
						   PARSE_INTERFACE, interface);

			attr[0] = "type";
			attr[1] = "s";
			attr[2] = "access";
			attr[3] = "read";
			attr[4] = NULL;
		}

		ret = property_start_tag (xmlp, "property", attr);

		TEST_LT (ret, 0);

		TEST_EQ_P (parse_stack_top (&context.stack), parent);

		TEST_LIST_EMPTY (&interface->properties);

		err = nih_error_get ();
		TEST_EQ (err->number, PROPERTY_MISSING_NAME);
		nih_free (err);

		nih_free (parent);
	}


	/* Check that a property with an invalid name results in an
	 * error being raised.
	 */
	TEST_FEATURE ("with invalid name");
	interface = interface_new (NULL, "com.netsplit.Nih.Test");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_INTERFACE, interface);

	attr[0] = "name";
	attr[1] = "Test Property";
	attr[2] = "type";
	attr[3] = "s";
	attr[4] = "access";
	attr[5] = "readwrite";
	attr[6] = NULL;

	ret = property_start_tag (xmlp, "property", attr);

	TEST_LT (ret, 0);

	TEST_EQ_P (parse_stack_top (&context.stack), parent);

	TEST_LIST_EMPTY (&interface->properties);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_INVALID_NAME);
	nih_free (err);

	nih_free (parent);


	/* Check that a property with a missing type attribute results
	 * in an error being raised.
	 */
	TEST_FEATURE ("with missing type");
	interface = interface_new (NULL, "com.netsplit.Nih.Test");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_INTERFACE, interface);

	attr[0] = "name";
	attr[1] = "TestProperty";
	attr[2] = "access";
	attr[3] = "read";
	attr[4] = NULL;

	ret = property_start_tag (xmlp, "property", attr);

	TEST_LT (ret, 0);

	TEST_EQ_P (parse_stack_top (&context.stack), parent);

	TEST_LIST_EMPTY (&interface->properties);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_MISSING_TYPE);
	nih_free (err);

	nih_free (parent);


	/* Check that a property with an invalid type results in an
	 * error being raised.
	 */
	TEST_FEATURE ("with invalid type");
	interface = interface_new (NULL, "com.netsplit.Nih.Test");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_INTERFACE, interface);

	attr[0] = "name";
	attr[1] = "TestProperty";
	attr[2] = "type";
	attr[3] = "si";
	attr[4] = "access";
	attr[5] = "readwrite";
	attr[6] = NULL;

	ret = property_start_tag (xmlp, "property", attr);

	TEST_LT (ret, 0);

	TEST_EQ_P (parse_stack_top (&context.stack), parent);

	TEST_LIST_EMPTY (&interface->properties);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_INVALID_TYPE);
	nih_free (err);

	nih_free (parent);


	/* Check that a property with a missing access attribute results
	 * in an error being raised.
	 */
	TEST_FEATURE ("with missing access");
	interface = interface_new (NULL, "com.netsplit.Nih.Test");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_INTERFACE, interface);

	attr[0] = "name";
	attr[1] = "TestProperty";
	attr[2] = "type";
	attr[3] = "s";
	attr[4] = NULL;

	ret = property_start_tag (xmlp, "property", attr);

	TEST_LT (ret, 0);

	TEST_EQ_P (parse_stack_top (&context.stack), parent);

	TEST_LIST_EMPTY (&interface->properties);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_MISSING_ACCESS);
	nih_free (err);

	nih_free (parent);


	/* Check that a property with an invalid access results in an
	 * error being raised.
	 */
	TEST_FEATURE ("with invalid access");
	interface = interface_new (NULL, "com.netsplit.Nih.Test");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_INTERFACE, interface);

	attr[0] = "name";
	attr[1] = "TestProperty";
	attr[2] = "type";
	attr[3] = "s";
	attr[4] = "access";
	attr[5] = "sideways";
	attr[6] = NULL;

	ret = property_start_tag (xmlp, "property", attr);

	TEST_LT (ret, 0);

	TEST_EQ_P (parse_stack_top (&context.stack), parent);

	TEST_LIST_EMPTY (&interface->properties);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_ILLEGAL_ACCESS);
	nih_free (err);

	nih_free (parent);


	/* Check that an unknown property attribute results in a warning
	 * being printed to standard error, but is otherwise ignored
	 * and the normal processing finished.
	 */
	TEST_FEATURE ("with unknown attribute");
	interface = interface_new (NULL, "com.netsplit.Nih.Test");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_INTERFACE, interface);

	attr[0] = "name";
	attr[1] = "TestProperty";
	attr[2] = "type";
	attr[3] = "s";
	attr[4] = "access";
	attr[5] = "read";
	attr[6] = "frodo";
	attr[7] = "baggins";
	attr[8] = NULL;

	TEST_DIVERT_STDERR (output) {
		ret = property_start_tag (xmlp, "property", attr);
	}
	rewind (output);

	TEST_EQ (ret, 0);

	entry = parse_stack_top (&context.stack);
	TEST_NE_P (entry, parent);
	TEST_ALLOC_SIZE (entry, sizeof (ParseStack));
	TEST_EQ (entry->type, PARSE_PROPERTY);

	property = entry->property;
	TEST_ALLOC_SIZE (property, sizeof (Property));
	TEST_ALLOC_PARENT (property, entry);
	TEST_EQ_STR (property->name, "TestProperty");
	TEST_ALLOC_PARENT (property->name, property);
	TEST_EQ_P (property->symbol, NULL);

	TEST_LIST_EMPTY (&interface->properties);

	TEST_FILE_EQ (output, ("test:foo:1:0: Ignored unknown <property> attribute: "
			       "frodo\n"));
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	nih_free (entry);
	nih_free (parent);


	/* Check that a property on an empty stack (ie. a top-level
	 * property element) results in a warning being printed on
	 * standard error and an ignored element being pushed onto the
	 * stack.
	 */
	TEST_FEATURE ("with empty stack");
	attr[0] = "name";
	attr[1] = "TestProperty";
	attr[2] = "type";
	attr[3] = "s";
	attr[4] = "access";
	attr[5] = "read";
	attr[6] = NULL;

	TEST_DIVERT_STDERR (output) {
		ret = property_start_tag (xmlp, "property", attr);
	}
	rewind (output);

	TEST_EQ (ret, 0);

	entry = parse_stack_top (&context.stack);
	TEST_ALLOC_SIZE (entry, sizeof (ParseStack));
	TEST_EQ (entry->type, PARSE_IGNORED);
	TEST_EQ_P (entry->data, NULL);

	TEST_FILE_EQ (output, "test:foo:1:0: Ignored unexpected <property> tag\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	nih_free (entry);


	/* Check that a property on top of a stack entry that's not an
	 * interface results in a warning being printed on
	 * standard error and an ignored element being pushed onto the
	 * stack.
	 */
	TEST_FEATURE ("with non-interface on stack");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_NODE, node_new (NULL, NULL));

	attr[0] = "name";
	attr[1] = "TestProperty";
	attr[2] = "type";
	attr[3] = "s";
	attr[4] = "access";
	attr[5] = "read";
	attr[6] = NULL;

	TEST_DIVERT_STDERR (output) {
		ret = property_start_tag (xmlp, "property", attr);
	}
	rewind (output);

	TEST_EQ (ret, 0);

	entry = parse_stack_top (&context.stack);
	TEST_NE_P (entry, parent);
	TEST_ALLOC_SIZE (entry, sizeof (ParseStack));
	TEST_EQ (entry->type, PARSE_IGNORED);
	TEST_EQ_P (entry->data, NULL);

	TEST_FILE_EQ (output, "test:foo:1:0: Ignored unexpected <property> tag\n");
	TEST_FILE_END (output);
	TEST_FILE_RESET (output);

	nih_free (entry);
	nih_free (parent);


	XML_ParserFree (xmlp);
	fclose (output);
}

void
test_end_tag (void)
{
	ParseContext context;
	ParseStack * parent = NULL;
	ParseStack * entry = NULL;
	XML_Parser   xmlp;
	Interface *  interface = NULL;
	Property *   property = NULL;
	Property *   other = NULL;
	int          ret;
	NihError *   err;

	TEST_FUNCTION ("property_end_tag");
	context.parent = NULL;
	nih_list_init (&context.stack);
	context.filename = "foo";
	context.node = NULL;

	assert (xmlp = XML_ParserCreate ("UTF-8"));
	XML_SetUserData (xmlp, &context);


	/* Check that when we parse the end tag for a property, we pop
	 * the Property object off the stack (freeing and removing it)
	 * and append it to the parent interface's properties list, adding a
	 * reference to the interface as well.  A symbol should be generated
	 * for the property by convering its name to C style.
	 */
	TEST_FEATURE ("with no assigned symbol");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			interface = interface_new (NULL, "com.netsplit.Nih.Test");
			parent = parse_stack_push (NULL, &context.stack,
						   PARSE_INTERFACE, interface);

			property = property_new (NULL, "TestProperty", "s",
						 NIH_DBUS_READ);
			entry = parse_stack_push (NULL, &context.stack,
						  PARSE_PROPERTY, property);
		}

		TEST_FREE_TAG (entry);

		ret = property_end_tag (xmlp, "property");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			TEST_NOT_FREE (entry);
			TEST_LIST_EMPTY (&interface->properties);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (entry);
			nih_free (parent);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FREE (entry);
		TEST_ALLOC_PARENT (property, interface);

		TEST_LIST_NOT_EMPTY (&interface->properties);
		TEST_EQ_P (interface->properties.next, &property->entry);

		TEST_EQ_STR (property->symbol, "test_property");
		TEST_ALLOC_PARENT (property->symbol, property);

		nih_free (parent);
	}


	/* Check that when the symbol has been pre-assigned by the data,
	 * it's not overridden and is used even if different.
	 */
	TEST_FEATURE ("with assigned symbol");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			interface = interface_new (NULL, "com.netsplit.Nih.Test");
			parent = parse_stack_push (NULL, &context.stack,
						   PARSE_INTERFACE, interface);

			property = property_new (NULL, "TestProperty",
						 "s", NIH_DBUS_READ);
			property->symbol = nih_strdup (property, "foo");
			entry = parse_stack_push (NULL, &context.stack,
						  PARSE_PROPERTY, property);
		}

		TEST_FREE_TAG (entry);

		ret = property_end_tag (xmlp, "property");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			TEST_NOT_FREE (entry);
			TEST_LIST_EMPTY (&interface->properties);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (entry);
			nih_free (parent);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FREE (entry);
		TEST_ALLOC_PARENT (property, interface);

		TEST_LIST_NOT_EMPTY (&interface->properties);
		TEST_EQ_P (interface->properties.next, &property->entry);

		TEST_EQ_STR (property->symbol, "foo");
		TEST_ALLOC_PARENT (property->symbol, property);

		nih_free (parent);
	}


	/* Check that we don't generate a duplicate symbol, and instead
	 * raise an error and allow the user to deal with it using
	 * the Symbol annotation.  The reason we don't work around this
	 * with a counter or similar is that the function names then
	 * become unpredicatable (introspection data isn't ordered).
	 */
	TEST_FEATURE ("with conflicting symbol");
	interface = interface_new (NULL, "com.netsplit.Nih.Test");
	parent = parse_stack_push (NULL, &context.stack,
				   PARSE_INTERFACE, interface);

	other = property_new (interface, "Test", "s", NIH_DBUS_READ);
	other->symbol = nih_strdup (other, "test_property");
	nih_list_add (&interface->properties, &other->entry);

	property = property_new (NULL, "TestProperty", "s", NIH_DBUS_READ);
	entry = parse_stack_push (NULL, &context.stack,
				  PARSE_PROPERTY, property);

	ret = property_end_tag (xmlp, "property");

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_DUPLICATE_SYMBOL);
	nih_free (err);

	nih_free (entry);
	nih_free (parent);


	XML_ParserFree (xmlp);
}


void
test_annotation (void)
{
	Property *property = NULL;
	char *    symbol;
	int       ret;
	NihError *err;

	TEST_FUNCTION ("property_annotation");


	/* Check that the annotation to mark a property as deprecated is
	 * handled, and the Property is marked deprecated.
	 */
	TEST_FEATURE ("with deprecated annotation");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			property = property_new (NULL, "TestProperty", "s",
						 NIH_DBUS_READ);
		}

		ret = property_annotation (property,
					   "org.freedesktop.DBus.Deprecated",
					   "true");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			TEST_FALSE (property->deprecated);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (property);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_TRUE (property->deprecated);

		nih_free (property);
	}


	/* Check that the annotation to mark a property as deprecated can be
	 * given a false value to explicitly mark the Property non-deprecated.
	 */
	TEST_FEATURE ("with explicitly non-deprecated annotation");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			property = property_new (NULL, "TestProperty", "s",
						 NIH_DBUS_READ);
			property->deprecated = TRUE;
		}

		ret = property_annotation (property,
					   "org.freedesktop.DBus.Deprecated",
					   "false");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			TEST_TRUE (property->deprecated);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (property);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FALSE (property->deprecated);

		nih_free (property);
	}


	/* Check that an annotation to add a symbol to the property is
	 * handled, and the new symbol is stored in the property.
	 */
	TEST_FEATURE ("with symbol annotation");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			property = property_new (NULL, "TestProperty", "s",
						 NIH_DBUS_READ);
		}

		ret = property_annotation (property,
					   "com.netsplit.Nih.Symbol",
					   "foo");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (property);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_EQ_STR (property->symbol, "foo");
		TEST_ALLOC_PARENT (property->symbol, property);

		nih_free (property);
	}


	/* Check that an annotation to add a symbol to the property
	 * replaces any previous symbol applied (e.g. by a previous
	 * annotation).
	 */
	TEST_FEATURE ("with symbol annotation and existing symbol");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			property = property_new (NULL, "TestProperty", "s",
						 NIH_DBUS_READ);
			property->symbol = nih_strdup (property, "test_arg");
		}

		symbol = property->symbol;
		TEST_FREE_TAG (symbol);

		ret = property_annotation (property,
					   "com.netsplit.Nih.Symbol",
					   "foo");

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (property);
			continue;
		}

		TEST_EQ (ret, 0);

		TEST_FREE (symbol);

		TEST_EQ_STR (property->symbol, "foo");
		TEST_ALLOC_PARENT (property->symbol, property);

		nih_free (property);
	}


	/* Check that an invalid value for the deprecated annotation results
	 * in an error being raised.
	 */
	TEST_FEATURE ("with invalid value for deprecated annotation");
	property = property_new (NULL, "TestProperty", "s", NIH_DBUS_READ);

	ret = property_annotation (property,
				   "org.freedesktop.DBus.Deprecated",
				   "foo");

	TEST_LT (ret, 0);

	TEST_EQ_P (property->symbol, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_ILLEGAL_DEPRECATED);
	nih_free (err);

	nih_free (property);


	/* Check that an invalid symbol in an annotation results in an
	 * error being raised.
	 */
	TEST_FEATURE ("with invalid symbol in annotation");
	property = property_new (NULL, "TestProperty", "s", NIH_DBUS_READ);

	ret = property_annotation (property,
				   "com.netsplit.Nih.Symbol",
				   "foo bar");

	TEST_LT (ret, 0);

	TEST_EQ_P (property->symbol, NULL);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_INVALID_SYMBOL);
	nih_free (err);

	nih_free (property);


	/* Check that an unknown annotation results in an error being
	 * raised.
	 */
	TEST_FEATURE ("with unknown annotation");
	property = property_new (NULL, "TestProperty", "s", NIH_DBUS_READ);

	ret = property_annotation (property,
				   "com.netsplit.Nih.Unknown",
				   "true");

	TEST_LT (ret, 0);

	err = nih_error_get ();
	TEST_EQ (err->number, PROPERTY_UNKNOWN_ANNOTATION);
	nih_free (err);

	nih_free (property);
}


static int my_property_get_called = 0;

int
my_property_get (void *          data,
		 NihDBusMessage *message,
		 char **         str)
{
	my_property_get_called++;

	TEST_EQ_P (data, NULL);

	TEST_ALLOC_SIZE (message, sizeof (NihDBusMessage));
	TEST_NE_P (message->conn, NULL);
	TEST_NE_P (message->message, NULL);

	TEST_NE_P (str, NULL);

	*str = nih_strdup (message, "dog and doughnut");
	if (! *str)
		return -1;

	return 0;
}

void
test_object_get_function (void)
{
	pid_t             dbus_pid;
	DBusConnection *  server_conn;
	DBusConnection *  client_conn;
	NihList           prototypes;
	NihList           handlers;
	Property *        property = NULL;
	char *            iface;
	char *            name;
	char *            str;
	TypeFunc *        func;
	TypeVar *         arg;
	NihListEntry *    attrib;
	DBusMessage *     method_call;
	DBusMessageIter   iter;
	DBusMessageIter   subiter;
	DBusMessage *     reply;
	NihDBusMessage *  message;
	NihDBusObject *   object;
	dbus_uint32_t     serial;
	int               ret;

	TEST_FUNCTION ("property_object_get_function");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can generate a function that marshals a value
	 * obtained by calling a property handler function into a variant
	 * appended to the message iterator passed.
	 */
	TEST_FEATURE ("with property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);
		nih_list_init (&handlers);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
		}

		str = property_object_get_function (NULL, property,
						    "MyProperty_get",
						    "my_property_get",
						    &prototypes, &handlers);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);
			TEST_LIST_EMPTY (&handlers);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "MyProperty_get (NihDBusObject *  object,\n"
				   "                NihDBusMessage * message,\n"
				   "                DBusMessageIter *iter)\n"
				   "{\n"
				   "\tDBusMessageIter variter;\n"
				   "\tchar *          value;\n"
				   "\n"
				   "\tnih_assert (object != NULL);\n"
				   "\tnih_assert (message != NULL);\n"
				   "\tnih_assert (iter != NULL);\n"
				   "\n"
				   "\t/* Call the handler function */\n"
				   "\tif (my_property_get (object->data, message, &value) < 0)\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\t/* Append a variant onto the message to contain the property value. */\n"
				   "\tif (! dbus_message_iter_open_container (iter, DBUS_TYPE_VARIANT, \"s\", &variter))\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\t/* Marshal a char * onto the message */\n"
				   "\tif (! dbus_message_iter_append_basic (&variter, DBUS_TYPE_STRING, &value)) {\n"
				   "\t\tdbus_message_iter_close_container (iter, &variter);\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\t/* Finish the variant */\n"
				   "\tif (! dbus_message_iter_close_container (iter, &variter))\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\treturn 0;\n"
				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "MyProperty_get");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusObject *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "object");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "DBusMessageIter *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "iter");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		TEST_LIST_NOT_EMPTY (&handlers);

		func = (TypeFunc *)handlers.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_get");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "void *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "data");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "char **");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&handlers);

		nih_free (str);
		nih_free (property);
	}


	/* Check that we can use the generated code to get the value of a
	 * property for a reply we're generating.  The handler function
	 * should be called and the value appended to our message inside
	 * a variant.
	 */
	TEST_FEATURE ("with property (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Get");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		reply = dbus_message_new_method_return (method_call);

		dbus_message_iter_init_append (reply, &iter);

		my_property_get_called = 0;

		ret = MyProperty_get (object, message, &iter);

		if (test_alloc_failed
		    && (ret < 0)) {
			dbus_message_unref (reply);
			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_TRUE (my_property_get_called);
		TEST_EQ (ret, 0);

		dbus_message_iter_init (reply, &iter);

		TEST_EQ (dbus_message_iter_get_arg_type (&iter),
			 DBUS_TYPE_VARIANT);

		dbus_message_iter_recurse (&iter, &subiter);

		TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
			 DBUS_TYPE_STRING);

		dbus_message_iter_get_basic (&subiter, &str);
		TEST_EQ_STR (str, "dog and doughnut");

		dbus_message_iter_next (&subiter);

		TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
			 DBUS_TYPE_INVALID);

		dbus_message_iter_next (&iter);

		TEST_EQ (dbus_message_iter_get_arg_type (&iter),
			 DBUS_TYPE_INVALID);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (reply);
		dbus_message_unref (method_call);
	}


	/* Check that when we generate a function for a deprecated
	 * property, we don't include the attribute since we don't
	 * want gcc warnings when implementing an object.
	 */
	TEST_FEATURE ("with deprecated property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);
		nih_list_init (&handlers);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
			property->deprecated = TRUE;
		}

		str = property_object_get_function (NULL, property,
						    "MyProperty_get",
						    "my_property_get",
						    &prototypes, &handlers);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);
			TEST_LIST_EMPTY (&handlers);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "MyProperty_get (NihDBusObject *  object,\n"
				   "                NihDBusMessage * message,\n"
				   "                DBusMessageIter *iter)\n"
				   "{\n"
				   "\tDBusMessageIter variter;\n"
				   "\tchar *          value;\n"
				   "\n"
				   "\tnih_assert (object != NULL);\n"
				   "\tnih_assert (message != NULL);\n"
				   "\tnih_assert (iter != NULL);\n"
				   "\n"
				   "\t/* Call the handler function */\n"
				   "\tif (my_property_get (object->data, message, &value) < 0)\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\t/* Append a variant onto the message to contain the property value. */\n"
				   "\tif (! dbus_message_iter_open_container (iter, DBUS_TYPE_VARIANT, \"s\", &variter))\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\t/* Marshal a char * onto the message */\n"
				   "\tif (! dbus_message_iter_append_basic (&variter, DBUS_TYPE_STRING, &value)) {\n"
				   "\t\tdbus_message_iter_close_container (iter, &variter);\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\t/* Finish the variant */\n"
				   "\tif (! dbus_message_iter_close_container (iter, &variter))\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\treturn 0;\n"
				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "MyProperty_get");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusObject *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "object");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "DBusMessageIter *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "iter");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		TEST_LIST_NOT_EMPTY (&handlers);

		func = (TypeFunc *)handlers.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_get");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "void *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "data");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "char **");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&handlers);

		nih_free (str);
		nih_free (property);
	}


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


static int my_property_set_called = 0;

int
my_property_set (void *          data,
		 NihDBusMessage *message,
		 const char *    str)
{
	nih_local char *dup = NULL;

	my_property_set_called++;

	TEST_EQ_P (data, NULL);

	TEST_ALLOC_SIZE (message, sizeof (NihDBusMessage));
	TEST_NE_P (message->conn, NULL);
	TEST_NE_P (message->message, NULL);

	TEST_ALLOC_PARENT (str, message);

	if (! strcmp (str, "dog and doughnut")) {
		dup = nih_strdup (NULL, str);
		if (! dup)
			nih_return_no_memory_error (-1);

		return 0;

	} else if (! strcmp (str, "felch and firkin")) {
		nih_dbus_error_raise ("com.netsplit.Nih.MyProperty.Fail",
				      "Bad value for my_property");
		return -1;

	} else if (! strcmp (str, "fruitbat and ball")) {
		nih_error_raise (EBADF, strerror (EBADF));
		return -1;
	}

	return 0;
}

void
test_object_set_function (void)
{
	pid_t             dbus_pid;
	DBusConnection *  server_conn;
	DBusConnection *  client_conn;
	NihList           prototypes;
	NihList           handlers;
	Property *        property = NULL;
	char *            iface;
	char *            name;
	char *            str;
	TypeFunc *        func;
	TypeVar *         arg;
	NihListEntry *    attrib;
	double            double_arg;
	DBusMessage *     method_call;
	DBusMessageIter   iter;
	DBusMessageIter   subiter;
	NihDBusMessage *  message;
	NihDBusObject *   object;
	dbus_uint32_t     serial;
	int               ret;
	NihError *        err;
	NihDBusError *    dbus_err;

	TEST_FUNCTION ("property_object_set_function");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can generate a function that demarshals a value
	 * from a variant in the passed message iterator, calls a handler
	 * function to set that property and returns to indicate success
	 * or error.
	 */
	TEST_FEATURE ("with property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);
		nih_list_init (&handlers);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
		}

		str = property_object_set_function (NULL, property,
						    "MyProperty_set",
						    "my_property_set",
						    &prototypes, &handlers);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);
			TEST_LIST_EMPTY (&handlers);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "MyProperty_set (NihDBusObject *  object,\n"
				   "                NihDBusMessage * message,\n"
				   "                DBusMessageIter *iter)\n"
				   "{\n"
				   "\tDBusMessageIter variter;\n"
				   "\tconst char *    value_dbus;\n"
				   "\tchar *          value;\n"
				   "\n"
				   "\tnih_assert (object != NULL);\n"
				   "\tnih_assert (message != NULL);\n"
				   "\tnih_assert (iter != NULL);\n"
				   "\n"
				   "\t/* Recurse into the variant */\n"
				   "\tif (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_VARIANT) {\n"
				   "\t\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				   "\t\t                             _(\"Invalid arguments to my_property property\"));\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_recurse (iter, &variter);\n"
				   "\n"
				   "\t/* Demarshal a char * from the message */\n"
				   "\tif (dbus_message_iter_get_arg_type (&variter) != DBUS_TYPE_STRING) {\n"
				   "\t\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				   "\t\t                             _(\"Invalid arguments to my_property property\"));\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_get_basic (&variter, &value_dbus);\n"
				   "\n"
				   "\tvalue = nih_strdup (message, value_dbus);\n"
				   "\tif (! value) {\n"
				   "\t\tnih_error_raise_no_memory ();\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_next (&variter);\n"
				   "\n"
				   "\tdbus_message_iter_next (iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_INVALID) {\n"
				   "\t\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				   "\t\t                             _(\"Invalid arguments to my_property property\"));\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\t/* Call the handler function */\n"
				   "\tif (my_property_set (object->data, message, value) < 0)\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\treturn 0;\n"
				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "MyProperty_set");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusObject *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "object");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "DBusMessageIter *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "iter");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		TEST_LIST_NOT_EMPTY (&handlers);

		func = (TypeFunc *)handlers.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_set");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "void *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "data");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "const char *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&handlers);

		nih_free (str);
		nih_free (property);
	}


	/* Check that we can use the generated code to demarshal the
	 * property value from inside the variant in the method call,
	 * passing it to the handler function.
	 */
	TEST_FEATURE ("with property (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Set");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
						  DBUS_TYPE_STRING_AS_STRING,
						  &subiter);

		str = "dog and doughnut";
		dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
						&str);

		dbus_message_iter_close_container (&iter, &subiter);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		dbus_message_iter_init (method_call, &iter);

		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_VARIANT);

		my_property_set_called = 0;

		ret = MyProperty_set (object, message, &iter);

		if (test_alloc_failed) {
			TEST_LT (ret, 0);

			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_TRUE (my_property_set_called);
		TEST_EQ (ret, 0);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (method_call);
	}


	/* Check that if the handler raises a D-Bus error, it is returned
	 * to the caller.
	 */
	TEST_FEATURE ("with D-Bus error from handler (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Set");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
						  DBUS_TYPE_STRING_AS_STRING,
						  &subiter);

		str = "felch and firkin";
		dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
						&str);

		dbus_message_iter_close_container (&iter, &subiter);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		dbus_message_iter_init (method_call, &iter);

		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_VARIANT);

		my_property_set_called = 0;

		ret = MyProperty_set (object, message, &iter);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_TRUE (my_property_set_called);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, "com.netsplit.Nih.MyProperty.Fail");
		nih_free (err);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (method_call);
	}


	/* Check that if the handler raises a generic error, it is returned
	 * to the caller.
	 */
	TEST_FEATURE ("with generic error from handler (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Set");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
						  DBUS_TYPE_STRING_AS_STRING,
						  &subiter);

		str = "fruitbat and ball";
		dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
						&str);

		dbus_message_iter_close_container (&iter, &subiter);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		dbus_message_iter_init (method_call, &iter);

		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_VARIANT);

		my_property_set_called = 0;

		ret = MyProperty_set (object, message, &iter);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_TRUE (my_property_set_called);

		TEST_EQ (err->number, EBADF);
		nih_free (err);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (method_call);
	}


	/* Check that a missing argument to the property method call
	 * results in an invalid args error message being returned
	 * without the handler being called.
	 */
	TEST_FEATURE ("with missing argument to method (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Set");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		dbus_message_iter_init (method_call, &iter);

		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);

		my_property_set_called = 0;

		ret = MyProperty_set (object, message, &iter);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_FALSE (my_property_set_called);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, DBUS_ERROR_INVALID_ARGS);
		nih_free (err);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (method_call);
	}


	/* Check that a non-variant type in the property method call
	 * results in an invalid args error message being returned
	 * without the handler being called.
	 */
	TEST_FEATURE ("with invalid argument in method (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Set");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		double_arg = 3.14;
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_DOUBLE,
						&double_arg);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		dbus_message_iter_init (method_call, &iter);

		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);

		my_property_set_called = 0;

		ret = MyProperty_set (object, message, &iter);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_FALSE (my_property_set_called);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, DBUS_ERROR_INVALID_ARGS);
		nih_free (err);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (method_call);
	}


	/* Check that the wrong type in the variant in the property method call
	 * results in an invalid args error message being returned without
	 * the handler being called.
	 */
	TEST_FEATURE ("with invalid variant item in method (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Set");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
						  DBUS_TYPE_DOUBLE_AS_STRING,
						  &subiter);

		double_arg = 3.14;
		dbus_message_iter_append_basic (&subiter, DBUS_TYPE_DOUBLE,
						&double_arg);

		dbus_message_iter_close_container (&iter, &subiter);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		dbus_message_iter_init (method_call, &iter);

		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);

		my_property_set_called = 0;

		ret = MyProperty_set (object, message, &iter);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_FALSE (my_property_set_called);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, DBUS_ERROR_INVALID_ARGS);
		nih_free (err);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (method_call);
	}


	/* Check that an extra argument to the property method call
	 * results in an invalid args error message being returned
	 * without the handler being called.
	 */
	TEST_FEATURE ("with extra argument to method (generated code)");
	TEST_ALLOC_FAIL {
		method_call = dbus_message_new_method_call (
			dbus_bus_get_unique_name (client_conn),
			"/com/netsplit/Nih",
			"org.freedesktop.DBus.Properties",
			"Set");

		dbus_message_iter_init_append (method_call, &iter);

		iface = "com.netsplit.Nih.Test";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&iface);

		name = "my_property";
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING,
						&name);

		dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
						  DBUS_TYPE_STRING_AS_STRING,
						  &subiter);

		str = "dog and doughnut";
		dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
						&str);

		dbus_message_iter_close_container (&iter, &subiter);

		double_arg = 3.14;
		dbus_message_iter_append_basic (&iter, DBUS_TYPE_DOUBLE,
						&double_arg);

		dbus_connection_send (server_conn, method_call, &serial);
		dbus_connection_flush (server_conn);
		dbus_message_unref (method_call);

		TEST_DBUS_MESSAGE (client_conn, method_call);
		assert (dbus_message_get_serial (method_call) == serial);

		TEST_ALLOC_SAFE {
			message = nih_new (NULL, NihDBusMessage);
			message->conn = client_conn;
			message->message = method_call;

			object = nih_new (NULL, NihDBusObject);
			object->path = "/com/netsplit/Nih";
			object->conn = client_conn;
			object->data = NULL;
			object->interfaces = NULL;
			object->registered = TRUE;
		}

		dbus_message_iter_init (method_call, &iter);

		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);
		assert (dbus_message_iter_get_arg_type (&iter) == DBUS_TYPE_STRING);
		dbus_message_iter_next (&iter);

		my_property_set_called = 0;

		ret = MyProperty_set (object, message, &iter);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			nih_free (object);
			nih_free (message);
			dbus_message_unref (method_call);
			continue;
		}

		TEST_FALSE (my_property_set_called);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, DBUS_ERROR_INVALID_ARGS);
		nih_free (err);

		nih_free (object);
		nih_free (message);
		dbus_message_unref (method_call);
	}


	/* Check that a deprecated property does not have the attribute
	 * added, since we don't want gcc warnings when implementing
	 * objects.
	 */
	TEST_FEATURE ("with deprecated property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);
		nih_list_init (&handlers);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
			property->deprecated = TRUE;
		}

		str = property_object_set_function (NULL, property,
						    "MyProperty_set",
						    "my_property_set",
						    &prototypes, &handlers);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);
			TEST_LIST_EMPTY (&handlers);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "MyProperty_set (NihDBusObject *  object,\n"
				   "                NihDBusMessage * message,\n"
				   "                DBusMessageIter *iter)\n"
				   "{\n"
				   "\tDBusMessageIter variter;\n"
				   "\tconst char *    value_dbus;\n"
				   "\tchar *          value;\n"
				   "\n"
				   "\tnih_assert (object != NULL);\n"
				   "\tnih_assert (message != NULL);\n"
				   "\tnih_assert (iter != NULL);\n"
				   "\n"
				   "\t/* Recurse into the variant */\n"
				   "\tif (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_VARIANT) {\n"
				   "\t\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				   "\t\t                             _(\"Invalid arguments to my_property property\"));\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_recurse (iter, &variter);\n"
				   "\n"
				   "\t/* Demarshal a char * from the message */\n"
				   "\tif (dbus_message_iter_get_arg_type (&variter) != DBUS_TYPE_STRING) {\n"
				   "\t\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				   "\t\t                             _(\"Invalid arguments to my_property property\"));\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_get_basic (&variter, &value_dbus);\n"
				   "\n"
				   "\tvalue = nih_strdup (message, value_dbus);\n"
				   "\tif (! value) {\n"
				   "\t\tnih_error_raise_no_memory ();\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_next (&variter);\n"
				   "\n"
				   "\tdbus_message_iter_next (iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (iter) != DBUS_TYPE_INVALID) {\n"
				   "\t\tnih_dbus_error_raise_printf (DBUS_ERROR_INVALID_ARGS,\n"
				   "\t\t                             _(\"Invalid arguments to my_property property\"));\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\t/* Call the handler function */\n"
				   "\tif (my_property_set (object->data, message, value) < 0)\n"
				   "\t\treturn -1;\n"
				   "\n"
				   "\treturn 0;\n"
				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "MyProperty_set");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusObject *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "object");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "DBusMessageIter *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "iter");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		TEST_LIST_NOT_EMPTY (&handlers);

		func = (TypeFunc *)handlers.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_set");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "void *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "data");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusMessage *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "message");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "const char *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&handlers);

		nih_free (str);
		nih_free (property);
	}


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


void
test_proxy_get_sync_function (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	DBusConnection *client_conn;
	NihList         prototypes;
	Property *      property = NULL;
	char *          str;
	TypeFunc *      func;
	TypeVar *       arg;
	NihListEntry *  attrib;
	NihDBusProxy *  proxy = NULL;
	void *          parent = NULL;
	pid_t           pid;
	int             status;
	DBusMessage *   method_call;
	DBusMessage *   reply;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	char *          str_value;
	double          double_value;
	int             ret;
	NihError *      err;
	NihDBusError *  dbus_err;

	TEST_FUNCTION ("property_proxy_get_sync_function");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can generate a function that will make a method
	 * call to obtain the value of a property and return it in the
	 * pointer argument supplied.  The function returns an integer
	 * to indicate success.
	 */
	TEST_FEATURE ("with property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
		}

		str = property_proxy_get_sync_function (NULL,
							"com.netsplit.Nih.Test",
							property,
							"my_property_get_sync",
							&prototypes);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "my_property_get_sync (const void *  parent,\n"
				   "                      NihDBusProxy *proxy,\n"
				   "                      char **       value)\n"
				   "{\n"
				   "\tDBusMessage *   method_call;\n"
				   "\tDBusMessageIter iter;\n"
				   "\tDBusMessageIter variter;\n"
				   "\tDBusError       error;\n"
				   "\tDBusMessage *   reply;\n"
				   "\tconst char *    interface;\n"
				   "\tconst char *    property;\n"
				   "\tconst char *    local_dbus;\n"
				   "\tchar *          local;\n"
				   "\n"
				   "\tnih_assert (proxy != NULL);\n"
				   "\tnih_assert (value != NULL);\n"
				   "\n"
				   "\t/* Construct the method call message. */\n"
				   "\tmethod_call = dbus_message_new_method_call (proxy->name, proxy->path, \"org.freedesktop.DBus.Properties\", \"Get\");\n"
				   "\tif (! method_call)\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tdbus_message_iter_init_append (method_call, &iter);\n"
				   "\n"
				   "\tinterface = \"com.netsplit.Nih.Test\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &interface))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tproperty = \"my_property\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &property))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\t/* Send the message, and wait for the reply. */\n"
				   "\tdbus_error_init (&error);\n"
				   "\n"
				   "\treply = dbus_connection_send_with_reply_and_block (proxy->conn, method_call, -1, &error);\n"
				   "\tif (! reply) {\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\n"
				   "\t\tif (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY)) {\n"
				   "\t\t\tnih_error_raise_no_memory ();\n"
				   "\t\t} else {\n"
				   "\t\t\tnih_dbus_error_raise (error.name, error.message);\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_error_free (&error);\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_unref (method_call);\n"
				   "\n"
				   "\t/* Iterate the method arguments, recursing into the variant */\n"
				   "\tdbus_message_iter_init (reply, &iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_VARIANT) {\n"
				   "\t\tdbus_message_unref (reply);\n"
				   "\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_recurse (&iter, &variter);\n"
				   "\n"
				   "\tdbus_message_iter_next (&iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID) {\n"
				   "\t\tdbus_message_unref (reply);\n"
				   "\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t}\n"
				   "\n"
				   "\tdo {\n"
				   "\t\t__label__ enomem;\n"
				   "\n"
				   "\t\t/* Demarshal a char * from the message */\n"
				   "\t\tif (dbus_message_iter_get_arg_type (&variter) != DBUS_TYPE_STRING) {\n"
				   "\t\t\tdbus_message_unref (reply);\n"
				   "\t\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_message_iter_get_basic (&variter, &local_dbus);\n"
				   "\n"
				   "\t\tlocal = nih_strdup (parent, local_dbus);\n"
				   "\t\tif (! local) {\n"
				   "\t\t\t*value = NULL;\n"
				   "\t\t\tgoto enomem;\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_message_iter_next (&variter);\n"
				   "\n"
				   "\t\t*value = local;\n"
				   "\tenomem: __attribute__ ((unused));\n"
				   "\t} while (! *value);\n"
				   "\n"
				   "\tdbus_message_unref (reply);\n"
				   "\n"
				   "\treturn 0;\n"
				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_get_sync");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "const void *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "parent");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusProxy *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "proxy");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "char **");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		nih_free (str);
		nih_free (property);
	}


	/* Check that we can use the generated code to make a method call
	 * and obtain the value of the property.
	 */
	TEST_FEATURE ("with method call (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_method_return (method_call);
			dbus_message_unref (method_call);

			dbus_message_iter_init_append (reply, &iter);

			dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter);

			str_value = "wibble";
			dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value);

			dbus_message_iter_close_container (&iter, &subiter);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		str_value = NULL;

		ret = my_property_get_sync (parent, proxy, &str_value);

		if (test_alloc_failed
		    && (ret < 0)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (ret, 0);

		TEST_NE_P (str_value, NULL);
		TEST_ALLOC_PARENT (str_value, parent);
		TEST_EQ_STR (str_value, "wibble");

		nih_free (proxy);
	}


	/* Check that the generated code handles an error returned from
	 * the property get function, returning a raised error.
	 */
	TEST_FEATURE ("with error returned (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_error (method_call,
							"com.netsplit.Nih.Failed",
							"Didn't work out");
			dbus_message_unref (method_call);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		str_value = NULL;

		ret = my_property_get_sync (parent, proxy, &str_value);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, "com.netsplit.Nih.Failed");
		TEST_EQ_STR (err->message, "Didn't work out");
		nih_free (err);

		TEST_EQ_P (str_value, NULL);

		nih_free (proxy);
	}


	/* Check that an incorrect type in the variant results in the
	 * function returning a raised error.
	 */
	TEST_FEATURE ("with incorrect type in variant (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_method_return (method_call);
			dbus_message_unref (method_call);

			dbus_message_iter_init_append (reply, &iter);

			dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_DOUBLE_AS_STRING,
							  &subiter);

			double_value = 3.14;
			dbus_message_iter_append_basic (&subiter, DBUS_TYPE_DOUBLE,
							&double_value);

			dbus_message_iter_close_container (&iter, &subiter);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		str_value = NULL;

		ret = my_property_get_sync (parent, proxy, &str_value);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_INVALID_ARGS);
		nih_free (err);

		TEST_EQ_P (str_value, NULL);

		nih_free (proxy);
	}


	/* Check that an incorrect type in the arguments results in the
	 * function returning a raised error.
	 */
	TEST_FEATURE ("with incorrect type (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_method_return (method_call);
			dbus_message_unref (method_call);

			dbus_message_iter_init_append (reply, &iter);

			double_value = 3.14;
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_DOUBLE,
							&double_value);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		str_value = NULL;

		ret = my_property_get_sync (parent, proxy, &str_value);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_INVALID_ARGS);
		nih_free (err);

		TEST_EQ_P (str_value, NULL);

		nih_free (proxy);
	}


	/* Check that a missing argument results in the function
	 * returning a raised error.
	 */
	TEST_FEATURE ("with missing argument (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_method_return (method_call);
			dbus_message_unref (method_call);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		str_value = NULL;

		ret = my_property_get_sync (parent, proxy, &str_value);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_INVALID_ARGS);
		nih_free (err);

		TEST_EQ_P (str_value, NULL);

		nih_free (proxy);
	}


	/* Check that an extra arguments results in the function
	 * returning a raised error.
	 */
	TEST_FEATURE ("with extra argument (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Get"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_method_return (method_call);
			dbus_message_unref (method_call);

			dbus_message_iter_init_append (reply, &iter);

			dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT,
							  DBUS_TYPE_STRING_AS_STRING,
							  &subiter);

			str_value = "wibble";
			dbus_message_iter_append_basic (&subiter, DBUS_TYPE_STRING,
							&str_value);

			dbus_message_iter_close_container (&iter, &subiter);

			double_value = 3.14;
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_DOUBLE,
							&double_value);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		str_value = NULL;

		ret = my_property_get_sync (parent, proxy, &str_value);

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_INVALID_ARGS);
		nih_free (err);

		TEST_EQ_P (str_value, NULL);

		nih_free (proxy);
	}


	/* Check that a deprecated property has the deprecated attribute
	 * added to its function prototype, since we want to warn about
	 * client code using them.
	 */
	TEST_FEATURE ("with deprecated property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
			property->deprecated = TRUE;
		}

		str = property_proxy_get_sync_function (NULL,
							"com.netsplit.Nih.Test",
							property,
							"my_property_get_sync",
							&prototypes);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "my_property_get_sync (const void *  parent,\n"
				   "                      NihDBusProxy *proxy,\n"
				   "                      char **       value)\n"
				   "{\n"
				   "\tDBusMessage *   method_call;\n"
				   "\tDBusMessageIter iter;\n"
				   "\tDBusMessageIter variter;\n"
				   "\tDBusError       error;\n"
				   "\tDBusMessage *   reply;\n"
				   "\tconst char *    interface;\n"
				   "\tconst char *    property;\n"
				   "\tconst char *    local_dbus;\n"
				   "\tchar *          local;\n"
				   "\n"
				   "\tnih_assert (proxy != NULL);\n"
				   "\tnih_assert (value != NULL);\n"
				   "\n"
				   "\t/* Construct the method call message. */\n"
				   "\tmethod_call = dbus_message_new_method_call (proxy->name, proxy->path, \"org.freedesktop.DBus.Properties\", \"Get\");\n"
				   "\tif (! method_call)\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tdbus_message_iter_init_append (method_call, &iter);\n"
				   "\n"
				   "\tinterface = \"com.netsplit.Nih.Test\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &interface))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tproperty = \"my_property\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &property))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\t/* Send the message, and wait for the reply. */\n"
				   "\tdbus_error_init (&error);\n"
				   "\n"
				   "\treply = dbus_connection_send_with_reply_and_block (proxy->conn, method_call, -1, &error);\n"
				   "\tif (! reply) {\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\n"
				   "\t\tif (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY)) {\n"
				   "\t\t\tnih_error_raise_no_memory ();\n"
				   "\t\t} else {\n"
				   "\t\t\tnih_dbus_error_raise (error.name, error.message);\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_error_free (&error);\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_unref (method_call);\n"
				   "\n"
				   "\t/* Iterate the method arguments, recursing into the variant */\n"
				   "\tdbus_message_iter_init (reply, &iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_VARIANT) {\n"
				   "\t\tdbus_message_unref (reply);\n"
				   "\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_iter_recurse (&iter, &variter);\n"
				   "\n"
				   "\tdbus_message_iter_next (&iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID) {\n"
				   "\t\tdbus_message_unref (reply);\n"
				   "\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t}\n"
				   "\n"
				   "\tdo {\n"
				   "\t\t__label__ enomem;\n"
				   "\n"
				   "\t\t/* Demarshal a char * from the message */\n"
				   "\t\tif (dbus_message_iter_get_arg_type (&variter) != DBUS_TYPE_STRING) {\n"
				   "\t\t\tdbus_message_unref (reply);\n"
				   "\t\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_message_iter_get_basic (&variter, &local_dbus);\n"
				   "\n"
				   "\t\tlocal = nih_strdup (parent, local_dbus);\n"
				   "\t\tif (! local) {\n"
				   "\t\t\t*value = NULL;\n"
				   "\t\t\tgoto enomem;\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_message_iter_next (&variter);\n"
				   "\n"
				   "\t\t*value = local;\n"
				   "\tenomem: __attribute__ ((unused));\n"
				   "\t} while (! *value);\n"
				   "\n"
				   "\tdbus_message_unref (reply);\n"
				   "\n"
				   "\treturn 0;\n"
				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_get_sync");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "const void *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "parent");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusProxy *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "proxy");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "char **");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "deprecated");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		nih_free (str);
		nih_free (property);
	}


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}

void
test_proxy_set_sync_function (void)
{
	pid_t           dbus_pid;
	DBusConnection *server_conn;
	DBusConnection *client_conn;
	NihList         prototypes;
	Property *      property = NULL;
	char *          str;
	TypeFunc *      func;
	TypeVar *       arg;
	NihListEntry *  attrib;
	NihDBusProxy *  proxy = NULL;
	void *          parent = NULL;
	pid_t           pid;
	int             status;
	DBusMessage *   method_call;
	DBusMessage *   reply;
	DBusMessageIter iter;
	DBusMessageIter subiter;
	char *          str_value;
	double          double_value;
	int             ret;
	NihError *      err;
	NihDBusError *  dbus_err;

	TEST_FUNCTION ("property_proxy_set_sync_function");
	TEST_DBUS (dbus_pid);
	TEST_DBUS_OPEN (server_conn);
	TEST_DBUS_OPEN (client_conn);


	/* Check that we can generate a function that will make a method
	 * call to set the value of a property, returning an integer to
	 * indicate success.
	 */
	TEST_FEATURE ("with property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
		}

		str = property_proxy_set_sync_function (NULL,
							"com.netsplit.Nih.Test",
							property,
							"my_property_set_sync",
							&prototypes);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "my_property_set_sync (NihDBusProxy *proxy,\n"
				   "                      const char *  value)\n"
				   "{\n"
				   "\tDBusMessage *   method_call;\n"
				   "\tDBusMessageIter iter;\n"
				   "\tDBusMessageIter variter;\n"
				   "\tDBusError       error;\n"
				   "\tDBusMessage *   reply;\n"
				   "\tconst char *    interface;\n"
				   "\tconst char *    property;\n"
				   "\n"
				   "\tnih_assert (proxy != NULL);\n"
				   "\tnih_assert (value != NULL);\n"
				   "\n"
				   "\t/* Construct the method call message. */\n"
				   "\tmethod_call = dbus_message_new_method_call (proxy->name, proxy->path, \"org.freedesktop.DBus.Properties\", \"Set\");\n"
				   "\tif (! method_call)\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tdbus_message_iter_init_append (method_call, &iter);\n"
				   "\n"
				   "\tinterface = \"com.netsplit.Nih.Test\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &interface))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tproperty = \"my_property\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &property))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tif (! dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT, \"s\", &variter))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\t/* Marshal a char * onto the message */\n"
				   "\tif (! dbus_message_iter_append_basic (&variter, DBUS_TYPE_STRING, &value)) {\n"
				   "\t\tdbus_message_iter_close_container (&iter, &variter);\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\t}\n"
				   "\n"
				   "\tif (! dbus_message_iter_close_container (&iter, &variter)) {\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\t}\n"
				   "\n"
				   "\t/* Send the message, and wait for the reply. */\n"
				   "\tdbus_error_init (&error);\n"
				   "\n"
				   "\treply = dbus_connection_send_with_reply_and_block (proxy->conn, method_call, -1, &error);\n"
				   "\tif (! reply) {\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\n"
				   "\t\tif (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY)) {\n"
				   "\t\t\tnih_error_raise_no_memory ();\n"
				   "\t\t} else {\n"
				   "\t\t\tnih_dbus_error_raise (error.name, error.message);\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_error_free (&error);\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\t/* Check the reply has no arguments */\n"
				   "\tdbus_message_unref (method_call);\n"
				   "\tdbus_message_iter_init (reply, &iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID) {\n"
				   "\t\tdbus_message_unref (reply);\n"
				   "\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_unref (reply);\n"
				   "\n"
				   "\treturn 0;\n"
 				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_set_sync");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusProxy *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "proxy");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "const char *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		nih_free (str);
		nih_free (property);
	}


	/* Check that we can use the generated code to make a method call
	 * and set the value of the property.
	 */
	TEST_FEATURE ("with method call (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Set"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_VARIANT);

			dbus_message_iter_recurse (&iter, &subiter);

			TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&subiter, &str_value);
			TEST_EQ_STR (str_value, "wibble");

			dbus_message_iter_next (&subiter);

			TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
				 DBUS_TYPE_INVALID);

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_method_return (method_call);
			dbus_message_unref (method_call);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		ret = my_property_set_sync (proxy, "wibble");

		if (test_alloc_failed
		    && (ret < 0)) {
			err = nih_error_get ();
			TEST_EQ (err->number, ENOMEM);
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (ret, 0);

		nih_free (proxy);
	}


	/* Check that the generated code handles an error returned from
	 * the property get function, returning a raised error.
	 */
	TEST_FEATURE ("with error returned (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Set"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_VARIANT);

			dbus_message_iter_recurse (&iter, &subiter);

			TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&subiter, &str_value);
			TEST_EQ_STR (str_value, "wibble");

			dbus_message_iter_next (&subiter);

			TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
				 DBUS_TYPE_INVALID);

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_error (method_call,
							"com.netsplit.Nih.Failed",
							"Didn't work out");
			dbus_message_unref (method_call);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		ret = my_property_set_sync (proxy, "wibble");

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			TEST_EQ_P (str_value, NULL);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_ERROR);
		TEST_ALLOC_SIZE (err, sizeof (NihDBusError));
		dbus_err = (NihDBusError *)err;
		TEST_EQ_STR (dbus_err->name, "com.netsplit.Nih.Failed");
		TEST_EQ_STR (err->message, "Didn't work out");
		nih_free (err);

		nih_free (proxy);
	}


	/* Check that an extra arguments results in the function
	 * returning a raised error.
	 */
	TEST_FEATURE ("with extra argument (generated code)");
	TEST_ALLOC_FAIL {
		TEST_ALLOC_SAFE {
			proxy = nih_dbus_proxy_new (NULL, client_conn,
						    dbus_bus_get_unique_name (server_conn),
						    "/com/netsplit/Nih");
			parent = nih_alloc (proxy, 0);
		}

		TEST_CHILD (pid) {
			TEST_DBUS_MESSAGE (server_conn, method_call);

			/* Check the incoming message */
			TEST_TRUE (dbus_message_is_method_call (method_call,
								DBUS_INTERFACE_PROPERTIES,
								"Set"));

			dbus_message_iter_init (method_call, &iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "com.netsplit.Nih.Test");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&iter, &str_value);
			TEST_EQ_STR (str_value, "my_property");

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_VARIANT);

			dbus_message_iter_recurse (&iter, &subiter);

			TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
				 DBUS_TYPE_STRING);

			dbus_message_iter_get_basic (&subiter, &str_value);
			TEST_EQ_STR (str_value, "wibble");

			dbus_message_iter_next (&subiter);

			TEST_EQ (dbus_message_iter_get_arg_type (&subiter),
				 DBUS_TYPE_INVALID);

			dbus_message_iter_next (&iter);

			TEST_EQ (dbus_message_iter_get_arg_type (&iter),
				 DBUS_TYPE_INVALID);

			/* Construct and send the reply */
			reply = dbus_message_new_method_return (method_call);
			dbus_message_unref (method_call);

			dbus_message_iter_init_append (reply, &iter);

			double_value = 3.14;
			dbus_message_iter_append_basic (&iter, DBUS_TYPE_DOUBLE,
							&double_value);

			dbus_connection_send (server_conn, reply, NULL);
			dbus_connection_flush (server_conn);
			dbus_message_unref (reply);

			TEST_DBUS_CLOSE (client_conn);
			TEST_DBUS_CLOSE (server_conn);

			dbus_shutdown ();
			exit (0);
		}

		ret = my_property_set_sync (proxy, "wibble");

		TEST_LT (ret, 0);

		err = nih_error_get ();

		if (test_alloc_failed
		    && (err->number == ENOMEM)) {
			nih_free (err);

			/* If we failed with ENOMEM, the server must not
			 * have processed the reply
			 */
			kill (pid, SIGTERM);

			waitpid (pid, &status, 0);
			TEST_TRUE (WIFSIGNALED (status));
			TEST_EQ (WTERMSIG (status), SIGTERM);

			nih_free (proxy);
			continue;
		}

		waitpid (pid, &status, 0);
		TEST_TRUE (WIFEXITED (status));
		TEST_EQ (WEXITSTATUS (status), 0);

		TEST_EQ (err->number, NIH_DBUS_INVALID_ARGS);
		nih_free (err);

		nih_free (proxy);
	}


	/* Check that a deprecated property has the deprecated attribute
	 * added to its function prototype, since we want to warn against
	 * client code using this.
	 */
	TEST_FEATURE ("with deprecated property");
	TEST_ALLOC_FAIL {
		nih_list_init (&prototypes);

		TEST_ALLOC_SAFE {
			property = property_new (NULL, "my_property",
						 "s", NIH_DBUS_READWRITE);
			property->symbol = nih_strdup (property, "my_property");
			property->deprecated = TRUE;
		}

		str = property_proxy_set_sync_function (NULL,
							"com.netsplit.Nih.Test",
							property,
							"my_property_set_sync",
							&prototypes);

		if (test_alloc_failed) {
			TEST_EQ_P (str, NULL);

			TEST_LIST_EMPTY (&prototypes);

			nih_free (property);
			continue;
		}

		TEST_EQ_STR (str, ("int\n"
				   "my_property_set_sync (NihDBusProxy *proxy,\n"
				   "                      const char *  value)\n"
				   "{\n"
				   "\tDBusMessage *   method_call;\n"
				   "\tDBusMessageIter iter;\n"
				   "\tDBusMessageIter variter;\n"
				   "\tDBusError       error;\n"
				   "\tDBusMessage *   reply;\n"
				   "\tconst char *    interface;\n"
				   "\tconst char *    property;\n"
				   "\n"
				   "\tnih_assert (proxy != NULL);\n"
				   "\tnih_assert (value != NULL);\n"
				   "\n"
				   "\t/* Construct the method call message. */\n"
				   "\tmethod_call = dbus_message_new_method_call (proxy->name, proxy->path, \"org.freedesktop.DBus.Properties\", \"Set\");\n"
				   "\tif (! method_call)\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tdbus_message_iter_init_append (method_call, &iter);\n"
				   "\n"
				   "\tinterface = \"com.netsplit.Nih.Test\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &interface))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tproperty = \"my_property\";\n"
				   "\tif (! dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &property))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\tif (! dbus_message_iter_open_container (&iter, DBUS_TYPE_VARIANT, \"s\", &variter))\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\n"
				   "\t/* Marshal a char * onto the message */\n"
				   "\tif (! dbus_message_iter_append_basic (&variter, DBUS_TYPE_STRING, &value)) {\n"
				   "\t\tdbus_message_iter_close_container (&iter, &variter);\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\t}\n"
				   "\n"
				   "\tif (! dbus_message_iter_close_container (&iter, &variter)) {\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\t\tnih_return_no_memory_error (-1);\n"
				   "\t}\n"
				   "\n"
				   "\t/* Send the message, and wait for the reply. */\n"
				   "\tdbus_error_init (&error);\n"
				   "\n"
				   "\treply = dbus_connection_send_with_reply_and_block (proxy->conn, method_call, -1, &error);\n"
				   "\tif (! reply) {\n"
				   "\t\tdbus_message_unref (method_call);\n"
				   "\n"
				   "\t\tif (dbus_error_has_name (&error, DBUS_ERROR_NO_MEMORY)) {\n"
				   "\t\t\tnih_error_raise_no_memory ();\n"
				   "\t\t} else {\n"
				   "\t\t\tnih_dbus_error_raise (error.name, error.message);\n"
				   "\t\t}\n"
				   "\n"
				   "\t\tdbus_error_free (&error);\n"
				   "\t\treturn -1;\n"
				   "\t}\n"
				   "\n"
				   "\t/* Check the reply has no arguments */\n"
				   "\tdbus_message_unref (method_call);\n"
				   "\tdbus_message_iter_init (reply, &iter);\n"
				   "\n"
				   "\tif (dbus_message_iter_get_arg_type (&iter) != DBUS_TYPE_INVALID) {\n"
				   "\t\tdbus_message_unref (reply);\n"
				   "\t\tnih_return_error (-1, NIH_DBUS_INVALID_ARGS,\n"
				   "\t\t                  _(NIH_DBUS_INVALID_ARGS_STR));\n"
				   "\t}\n"
				   "\n"
				   "\tdbus_message_unref (reply);\n"
				   "\n"
				   "\treturn 0;\n"
 				   "}\n"));

		TEST_LIST_NOT_EMPTY (&prototypes);

		func = (TypeFunc *)prototypes.next;
		TEST_ALLOC_SIZE (func, sizeof (TypeFunc));
		TEST_ALLOC_PARENT (func, str);
		TEST_EQ_STR (func->type, "int");
		TEST_ALLOC_PARENT (func->type, func);
		TEST_EQ_STR (func->name, "my_property_set_sync");
		TEST_ALLOC_PARENT (func->name, func);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "NihDBusProxy *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "proxy");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);

		TEST_LIST_NOT_EMPTY (&func->args);

		arg = (TypeVar *)func->args.next;
		TEST_ALLOC_SIZE (arg, sizeof (TypeVar));
		TEST_ALLOC_PARENT (arg, func);
		TEST_EQ_STR (arg->type, "const char *");
		TEST_ALLOC_PARENT (arg->type, arg);
		TEST_EQ_STR (arg->name, "value");
		TEST_ALLOC_PARENT (arg->name, arg);
		nih_free (arg);
		TEST_LIST_EMPTY (&func->args);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "warn_unused_result");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_NOT_EMPTY (&func->attribs);

		attrib = (NihListEntry *)func->attribs.next;
		TEST_ALLOC_SIZE (attrib, sizeof (NihListEntry *));
		TEST_ALLOC_PARENT (attrib, func);
		TEST_EQ_STR (attrib->str, "deprecated");
		TEST_ALLOC_PARENT (attrib->str, attrib);
		nih_free (attrib);

		TEST_LIST_EMPTY (&func->attribs);
		nih_free (func);

		TEST_LIST_EMPTY (&prototypes);


		nih_free (str);
		nih_free (property);
	}


	TEST_DBUS_CLOSE (client_conn);
	TEST_DBUS_CLOSE (server_conn);
	TEST_DBUS_END (dbus_pid);

	dbus_shutdown ();
}


int
main (int   argc,
      char *argv[])
{
	program_name = "test";
	nih_error_init ();

	test_name_valid ();
	test_new ();
	test_start_tag ();
	test_end_tag ();
	test_annotation ();

	test_object_get_function ();
	test_object_set_function ();

	test_proxy_get_sync_function ();
	test_proxy_set_sync_function ();

	return 0;
}
