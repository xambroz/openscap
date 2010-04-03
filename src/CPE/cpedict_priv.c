/**
 * @file cpedict_priv.c
 * \brief Interface to Common Platform Enumeration (CPE) Language
 *
 * See more details at http://nvd.nist.gov/cpe.cfm
 */

/*
 * Copyright 2008 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      Maros Barabas <mbarabas@redhat.com>
 */

#include <libxml/xmlreader.h>
#include <stdlib.h>

#include "cpedict_priv.h"
#include "cpedict.h"
#include "cpeuri.h"

// for functions isspace
#include <ctype.h>
// for functins memset, strcpy
#include <string.h>

#include "../common/list.h"
#include "../common/elements.h"
#include "../_error.h"

/***************************************************************************/
/* Variable definitions
 * */

const char *PART_TO_CHAR[] = { NULL, "h", "o", "a" };

/* ****************************************
 * CPE-List structures
 * ***************************************/

/* <cpe-item>
 * */
struct cpe_item {		// the node <cpe-item>

	struct xml_metadata xml;
	struct cpe_name *name;	// CPE name as CPE URI
	struct oscap_list *titles;	// titles of cpe-item (can be in various languages)

	struct cpe_name *deprecated;	// CPE that deprecated this one (or NULL)
	char *deprecation_date;	// date of deprecation

	struct oscap_list *references;	// list of references
	struct oscap_list *checks;	// list of checks
	struct oscap_list *notes;	// list of notes - it's the same structure as titles
	struct cpe_item_metadata *metadata;	// element <meta:item-metadata>
};
OSCAP_GETTER(struct cpe_name *, cpe_item, name)
OSCAP_GETTER(struct cpe_name *, cpe_item, deprecated)
OSCAP_ACCESSOR_STRING(cpe_item, deprecation_date)
OSCAP_GETTER(struct cpe_item_metadata *, cpe_item, metadata)
OSCAP_IGETINS_GEN(cpe_reference, cpe_item, references, reference)
OSCAP_IGETINS_GEN(cpe_check, cpe_item, checks, check)
OSCAP_IGETINS(oscap_title, cpe_item, titles, title)
OSCAP_IGETINS(oscap_title, cpe_item, notes, note)
OSCAP_ITERATOR_REMOVE_F(cpe_reference) OSCAP_ITERATOR_REMOVE_F(cpe_check)

/* <cpe-item><item-metadata>
 * */
struct cpe_item_metadata {
	struct xml_metadata xml;
	char *modification_date;
	char *status;
	char *nvd_id;
	char *deprecated_by_nvd_id;
};
OSCAP_ACCESSOR_STRING(cpe_item_metadata, modification_date)
    OSCAP_ACCESSOR_STRING(cpe_item_metadata, status)
    OSCAP_ACCESSOR_STRING(cpe_item_metadata, nvd_id)
    OSCAP_ACCESSOR_STRING(cpe_item_metadata, deprecated_by_nvd_id)

/* <cpe-item><check>
 * */
struct cpe_check {
	struct xml_metadata xml;
	char *system;		// system check URI
	char *href;		// external file reference (NULL if not present)
	char *identifier;	// test identifier
};
OSCAP_ACCESSOR_STRING(cpe_check, system)
    OSCAP_ACCESSOR_STRING(cpe_check, href)
    OSCAP_ACCESSOR_STRING(cpe_check, identifier)

/* <cpe-item><references><reference>
 * */
struct cpe_reference {
	struct xml_metadata xml;
	char *href;		// reference URL
	char *content;		// reference description
};
OSCAP_ACCESSOR_STRING(cpe_reference, href)
    OSCAP_ACCESSOR_STRING(cpe_reference, content)

/* <generator>
 * */
struct cpe_generator {

	struct xml_metadata xml;
	char *product_name;	// generator software name
	char *product_version;	// generator software version
	char *schema_version;	// generator schema version
	char *timestamp;	// generation date and time
};
OSCAP_ACCESSOR_STRING(cpe_generator, product_name)
    OSCAP_ACCESSOR_STRING(cpe_generator, product_version)
    OSCAP_ACCESSOR_STRING(cpe_generator, schema_version)
    OSCAP_ACCESSOR_STRING(cpe_generator, timestamp)

/* <cpe-list>
 * */
struct cpe_dict_model {		// the main node

	struct oscap_list *xmlns;
	struct xml_metadata xml;
	struct oscap_list *items;	// dictionary items
	struct oscap_list *vendors;
	struct cpe_generator *generator;
};
OSCAP_IGETINS(xml_metadata, cpe_dict_model, xmlns, xml)
OSCAP_GETTER(struct cpe_generator *, cpe_dict_model, generator)
OSCAP_IGETTER_GEN(cpe_item, cpe_dict_model, items)
OSCAP_ITERATOR_REMOVE_F(cpe_item)
OSCAP_IGETINS_GEN(cpe_vendor, cpe_dict_model, vendors, vendor) OSCAP_ITERATOR_REMOVE_F(cpe_vendor)

/* ****************************************
 * Component-tree structures
 * ***************************************/
/* vendor
 * */
struct cpe_vendor {
	struct xml_metadata xml;
	char *value;
	struct oscap_list *titles;
	struct oscap_list *products;
};
OSCAP_ACCESSOR_STRING(cpe_vendor, value)
    OSCAP_IGETINS(oscap_title, cpe_vendor, titles, title)
    OSCAP_IGETINS_GEN(cpe_product, cpe_vendor, products, product)
    OSCAP_ITERATOR_REMOVE_F(cpe_product)

/* vendor -> product 
 * */
struct cpe_product {
	struct xml_metadata xml;
	char *value;
	cpe_part_t part;
	struct oscap_list *versions;
};
OSCAP_ACCESSOR_STRING(cpe_product, value)
    OSCAP_ACCESSOR_SIMPLE(cpe_part_t, cpe_product, part)
    OSCAP_IGETINS_GEN(cpe_version, cpe_product, versions, version)
    OSCAP_ITERATOR_REMOVE_F(cpe_version)

/* vendor -> product -> version 
 * */
struct cpe_version {
	struct xml_metadata xml;
	char *value;
	struct oscap_list *updates;
};
OSCAP_ACCESSOR_STRING(cpe_version, value)
    OSCAP_IGETINS_GEN(cpe_update, cpe_version, updates, update)
    OSCAP_ITERATOR_REMOVE_F(cpe_update)

/* vendor -> product -> version -> update 
 * */
struct cpe_update {
	struct xml_metadata xml;
	char *value;
	struct oscap_list *editions;
};
OSCAP_ACCESSOR_STRING(cpe_update, value)
    OSCAP_IGETINS_GEN(cpe_edition, cpe_update, editions, edition)
    OSCAP_ITERATOR_REMOVE_F(cpe_edition)

/* vendor -> product -> version -> update -> edition 
 * */
struct cpe_edition {
	struct xml_metadata xml;
	char *value;
	struct oscap_list *languages;
};
OSCAP_ACCESSOR_STRING(cpe_edition, value)
    OSCAP_IGETINS_GEN(cpe_language, cpe_edition, languages, language)
    OSCAP_ITERATOR_REMOVE_F(cpe_language)

/* vendor -> product -> version -> update -> edition -> language
 * */
struct cpe_language {
	struct xml_metadata xml;
	char *value;
};
OSCAP_ACCESSOR_STRING(cpe_language, value)

/* End of variable definitions
 * */
/***************************************************************************/
/***************************************************************************/
/* XML string variables definitions
 * */
#define TAG_CHECK_STR               BAD_CAST "check"
#define TAG_NOTES_STR               BAD_CAST "notes"
#define TAG_REFERENCES_STR          BAD_CAST "references"
#define ATTR_DEP_BY_NVDID_STR       BAD_CAST "deprecated-by-nvd-id"
#define ATTR_NVD_ID_STR             BAD_CAST "nvd-id"
#define ATTR_STATUS_STR             BAD_CAST "status"
#define ATTR_MODIFICATION_DATE_STR  BAD_CAST "modification-date"
#define TAG_ITEM_METADATA_STR       BAD_CAST "item-metadata"
#define TAG_REFERENCE_STR           BAD_CAST "reference"
#define TAG_NOTE_STR                BAD_CAST "note"
#define TAG_TITLE_STR               BAD_CAST "title"
#define TAG_CPE_ITEM_STR            BAD_CAST "cpe-item"
#define ATTR_DEPRECATION_DATE_STR   BAD_CAST "deprecation_date"
#define ATTR_DEPRECATED_BY_STR      BAD_CAST "deprecated_by"
#define ATTR_DEPRECATED_STR         BAD_CAST "deprecated"
#define ATTR_NAME_STR               BAD_CAST "name"
/* Generator */
#define TAG_GENERATOR_STR           BAD_CAST "generator"
#define TAG_PRODUCT_STR             BAD_CAST "product"
#define TAG_PRODUCT_NAME_STR        BAD_CAST "product_name"
#define TAG_PRODUCT_VERSION_STR     BAD_CAST "product_version"
#define TAG_SCHEMA_VERSION_STR      BAD_CAST "schema_version"
#define TAG_TIMESTAMP_STR           BAD_CAST "timestamp"
#define TAG_COMPONENT_TREE_STR      BAD_CAST "component-tree"
#define TAG_VENDOR_STR              BAD_CAST "vendor"
#define TAG_CPE_LIST_STR            BAD_CAST "cpe-list"
#define TAG_VERSION_STR             BAD_CAST "version"
#define TAG_UPDATE_STR              BAD_CAST "update"
#define TAG_EDITION_STR             BAD_CAST "edition"
#define TAG_LANGUAGE_STR            BAD_CAST "language"
#define ATTR_VALUE_STR      BAD_CAST "value"
#define ATTR_PART_STR       BAD_CAST "part"
#define ATTR_SYSTEM_STR     BAD_CAST "system"
#define ATTR_HREF_STR       BAD_CAST "href"
#define NS_META_STR         BAD_CAST "meta"
#define ATTR_XML_LANG_STR   BAD_CAST "xml:lang"
#define VAL_TRUE_STR        BAD_CAST "true"
/* End of XML string variables definitions
 * */
/***************************************************************************/
/***************************************************************************/
/* Declaration of static (private to this file) functions
 * These function shoud not be called from outside. For exporting these elements
 * has to call parent element's 
 */
static int xmlTextReaderNextElement(xmlTextReaderPtr reader);
static bool cpe_dict_model_add_item(struct cpe_dict_model *dict, struct cpe_item *item);

static struct cpe_reference *cpe_reference_parse(xmlTextReaderPtr reader);
static struct cpe_check *cpe_check_parse(xmlTextReaderPtr reader);

static void cpe_product_export(const struct cpe_product *product, xmlTextWriterPtr writer);
static void cpe_version_export(const struct cpe_version *version, xmlTextWriterPtr writer);
static void cpe_update_export(const struct cpe_update *update, xmlTextWriterPtr writer);
static void cpe_edition_export(const struct cpe_edition *edition, xmlTextWriterPtr writer);
static void cpe_language_export(const struct cpe_language *language, xmlTextWriterPtr writer);
static void cpe_note_export(const struct oscap_title *title, xmlTextWriterPtr writer);
static void cpe_check_export(const struct cpe_check *check, xmlTextWriterPtr writer);
static void cpe_reference_export(const struct cpe_reference *ref, xmlTextWriterPtr writer);

static bool cpe_validate_xml(const char *filename);
static int xmlTextReaderNextNode(xmlTextReaderPtr reader);
/***************************************************************************/

/* Add item to dictionary. Function that just check both variables
 * on NULL value.
 * */
static bool cpe_dict_model_add_item(struct cpe_dict_model *dict, struct cpe_item *item)
{

	__attribute__nonnull__(dict);
	__attribute__nonnull__(item);

	if (dict == NULL || item == NULL)
		return false;

	oscap_list_add(dict->items, item);
	return true;
}

/* Function that jump to next XML starting element.
 * */
static int xmlTextReaderNextElement(xmlTextReaderPtr reader)
{

	__attribute__nonnull__(reader);

	int ret;
	do {
		ret = xmlTextReaderRead(reader);
		// if end of file
		if (ret < 1)
			break;
	} while (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT);

	if (ret == -1) {
		oscap_setxmlerr(xmlCtxtGetLastError(reader));
		/* TODO: Should we end here as fatal ? */
	}

	return ret;
}

/* Function testing reader function 
 */
static int xmlTextReaderNextNode(xmlTextReaderPtr reader)
{

	__attribute__nonnull__(reader);

	int ret;
	ret = xmlTextReaderRead(reader);
	if (ret == -1) {
		oscap_setxmlerr(xmlCtxtGetLastError(reader));
		/* TODO: Should we end here as fatal ? */
	}

	return ret;
}

static bool cpe_validate_xml(const char *filename)
{

	__attribute__nonnull__(filename);

	xmlParserCtxtPtr ctxt;	/* the parser context */
	xmlDocPtr doc;		/* the resulting document tree */
	bool ret = false;

	/* create a parser context */
	ctxt = xmlNewParserCtxt();
	if (ctxt == NULL)
		return false;
	/* parse the file, activating the DTD validation option */
	doc = xmlCtxtReadFile(ctxt, filename, NULL, XML_PARSE_DTDATTR);
	/* check if parsing suceeded */
	if (doc == NULL) {
		xmlFreeParserCtxt(ctxt);
		oscap_setxmlerr(xmlCtxtGetLastError(ctxt));
		return false;
	}
	/* check if validation suceeded */
	if (ctxt->valid)
		ret = true;
	else			/* set xml error */
		oscap_setxmlerr(xmlCtxtGetLastError(ctxt));
	xmlFreeDoc(doc);
	/* free up the parser context */
	xmlFreeParserCtxt(ctxt);
	return ret;
}

/***************************************************************************/
/* Constructors of CPE structures cpe_*<structure>*_new()
 * More info in representive header file.
 * returns the type of <structure>
 */
struct cpe_dict_model *cpe_dict_model_new()
{

	struct cpe_dict_model *dict;

	dict = oscap_alloc(sizeof(struct cpe_dict_model));
	if (dict == NULL)
		return NULL;
	memset(dict, 0, sizeof(struct cpe_dict_model));

	dict->vendors = oscap_list_new();
	dict->items = oscap_list_new();
	dict->xmlns = oscap_list_new();

	dict->xml.lang = NULL;
	dict->xml.nspace = NULL;
	dict->xml.URI = NULL;

	return dict;
}

struct cpe_item_metadata *cpe_item_metadata_new()
{

	struct cpe_item_metadata *item;

	item = oscap_alloc(sizeof(struct cpe_item_metadata));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_item_metadata));

	item->modification_date = NULL;
	item->status = NULL;
	item->nvd_id = NULL;
	item->deprecated_by_nvd_id = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_item *cpe_item_new()
{

	struct cpe_item *item;

	item = oscap_alloc(sizeof(struct cpe_item));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_item));

	item->notes = oscap_list_new();
	item->references = oscap_list_new();
	item->checks = oscap_list_new();
	item->titles = oscap_list_new();

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_check *cpe_check_new()
{

	struct cpe_check *item;

	item = oscap_alloc(sizeof(struct cpe_check));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_check));

	item->system = NULL;
	item->href = NULL;
	item->identifier = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_reference *cpe_reference_new()
{

	struct cpe_reference *item;

	item = oscap_alloc(sizeof(struct cpe_reference));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_reference));

	item->href = NULL;
	item->content = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_generator *cpe_generator_new()
{

	struct cpe_generator *item;

	item = oscap_alloc(sizeof(struct cpe_generator));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_generator));

	item->product_name = NULL;
	item->product_version = NULL;
	item->schema_version = NULL;
	item->timestamp = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_vendor *cpe_vendor_new()
{

	struct cpe_vendor *item;

	item = oscap_alloc(sizeof(struct cpe_vendor));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_vendor));

	item->value = NULL;
	item->titles = oscap_list_new();
	item->products = oscap_list_new();

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_product *cpe_product_new()
{

	struct cpe_product *item;

	item = oscap_alloc(sizeof(struct cpe_product));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_product));

	item->versions = oscap_list_new();
	item->value = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_version *cpe_version_new()
{

	struct cpe_version *item;

	item = oscap_alloc(sizeof(struct cpe_version));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_version));

	item->updates = oscap_list_new();
	item->value = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_update *cpe_update_new()
{

	struct cpe_update *item;

	item = oscap_alloc(sizeof(struct cpe_update));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_update));

	item->editions = oscap_list_new();
	item->value = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_edition *cpe_edition_new()
{

	struct cpe_edition *item;

	item = oscap_alloc(sizeof(struct cpe_edition));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_edition));

	item->languages = oscap_list_new();
	item->value = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

struct cpe_language *cpe_language_new()
{

	struct cpe_language *item;

	item = oscap_alloc(sizeof(struct cpe_language));
	if (item == NULL)
		return NULL;
	memset(item, 0, sizeof(struct cpe_language));

	item->value = NULL;

	item->xml.lang = NULL;
	item->xml.nspace = NULL;
	item->xml.URI = NULL;

	return item;
}

/* End of CPE structures' contructors
 * */
/***************************************************************************/

/***************************************************************************/
/* Private parsing functions cpe_*<structure>*_parse( xmlTextReaderPtr )
 * More info in representive header file.
 * returns the type of <structure>
 */
struct cpe_dict_model *cpe_dict_model_parse_xml(const struct oscap_import_source *source)
{

	__attribute__nonnull__(source);

	xmlTextReaderPtr reader;
	struct cpe_dict_model *dict = NULL;

	if (!cpe_validate_xml(oscap_import_source_get_name(source)))
		return NULL;

	reader = xmlReaderForFile(oscap_import_source_get_name(source), NULL, 0);
	if (reader != NULL) {
		xmlTextReaderNextNode(reader);
		dict = cpe_dict_model_parse(reader);
	} else {
		oscap_seterr(OSCAP_EFAMILY_GLIBC, errno, "Unable to open file.");
	}
	xmlFreeTextReader(reader);
	return dict;
}

struct cpe_dict_model *cpe_dict_model_parse(xmlTextReaderPtr reader)
{

	__attribute__nonnull__(reader);

	struct cpe_dict_model *ret = NULL;
	struct cpe_item *item = NULL;
	struct xml_metadata *xml = NULL;
	struct cpe_vendor *vendor = NULL;
	int next_ret = 1;

	// let's find "<cpe-list>" element
	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CPE_LIST_STR) && (next_ret == 1)) {
		next_ret = xmlTextReaderNextElement(reader);
		// There is no "<cpe-list>" element :(( and we are at the end of file !
		if (next_ret == 0) {
			fprintf(stderr, "There is no \"cpe-list\" element in the provided xml tree !\n");
			return NULL;
		}
	}
	// we found cpe-list element, let's roll !
	// allocate memory for cpe_dict so we can fill items and vendors and general structures
	ret = cpe_dict_model_new();
	if (ret == NULL)
		return NULL;

	/* Reading XML namespaces */
	if (xmlTextReaderHasAttributes(reader) && xmlTextReaderMoveToFirstAttribute(reader) == 1) {
		do {
			xml = oscap_alloc(sizeof(struct xml_metadata));
			xml->lang = NULL;
			xml->nspace = oscap_strdup((char *)xmlTextReaderConstName(reader));
			xml->URI = oscap_strdup((char *)xmlTextReaderConstValue(reader));
			oscap_list_add(ret->xmlns, xml);
		} while (xmlTextReaderMoveToNextAttribute(reader) == 1);
	}
	// go through elements and switch through actions till end of file..
	next_ret = xmlTextReaderNextElement(reader);
	while (next_ret != 0) {

		if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GENERATOR_STR)) {	// <generator> | count = 1
			ret->generator = cpe_generator_parse(reader);
		} else if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CPE_ITEM_STR)) {	// <cpe-item> | cout = 0-n
			if ((item = cpe_item_parse(reader)) == NULL) {
				// something bad happend, let's try to recover and continue
				// add here some bad nodes list to write it to stdout after parsing is done
				// get the next node
				next_ret = xmlTextReaderNextElement(reader);
				continue;
			}
			// We got an item !
			if (!cpe_dict_model_add_item(ret, item)) {
				cpe_item_free(item);
				cpe_dict_model_free(ret);
				return NULL;
			}
			continue;
		} else if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VENDOR_STR)) {	// <vendor> | count = 0-n
			vendor = cpe_vendor_parse(reader);
			if (vendor)
				oscap_list_add(ret->vendors, vendor);
		} else
			// TODO: we need to store meta xml data of <component-tree> element
		if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_COMPONENT_TREE_STR)) {	// <vendor> | count = 0-n
			// we just need to jump over this element
		} else if (xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT) {
			oscap_seterr(OSCAP_EFAMILY_OSCAP, OSCAP_EXMLELEM, "Unknown XML element in CPE dictionary");
		}
		// get the next node
		next_ret = xmlTextReaderNextElement(reader);
	}

	return ret;
}

struct cpe_generator *cpe_generator_parse(xmlTextReaderPtr reader)
{

	__attribute__nonnull__(reader);

	struct cpe_generator *ret = NULL;

	if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GENERATOR_STR) &&
	    xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT) {

		// we are on "<generator>" element, let's alloc structure
		ret = cpe_generator_new();
		if (ret == NULL)
			return NULL;

		ret->xml.lang = (char *)xmlTextReaderConstXmlLang(reader);
		ret->xml.nspace = (char *)xmlTextReaderPrefix(reader);

		// skip nodes until new element
		xmlTextReaderNextElement(reader);

		while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_GENERATOR_STR) != 0) {

			if ((xmlStrcmp(xmlTextReaderConstLocalName(reader),
				       TAG_PRODUCT_NAME_STR) == 0) &&
			    (xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)) {
				ret->product_name = (char *)xmlTextReaderReadString(reader);
			} else
			    if ((xmlStrcmp(xmlTextReaderConstLocalName(reader),
					   TAG_PRODUCT_VERSION_STR) == 0) &&
				(xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)) {
				ret->product_version = (char *)xmlTextReaderReadString(reader);
			} else
			    if ((xmlStrcmp(xmlTextReaderConstLocalName(reader),
					   TAG_SCHEMA_VERSION_STR) == 0) &&
				(xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)) {
				ret->schema_version = (char *)xmlTextReaderReadString(reader);
			} else
			    if ((xmlStrcmp(xmlTextReaderConstLocalName(reader),
					   TAG_TIMESTAMP_STR) == 0) &&
				(xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT)) {
				ret->timestamp = (char *)xmlTextReaderReadString(reader);
			} else if (xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT) {
				oscap_seterr(OSCAP_EFAMILY_OSCAP, OSCAP_EXMLELEM,
					     "Unknown XML element in CPE dictionary generator");
			}
			// element saved. Let's jump on the very next one node (not element, because we need to 
			// find XML_READER_TYPE_END_ELEMENT node, see "while" condition and the condition below "while"
			xmlTextReaderNextNode(reader);

		}
	}

	return ret;

}

struct cpe_item *cpe_item_parse(xmlTextReaderPtr reader)
{

	struct cpe_item *ret = NULL;
	struct oscap_title *title = NULL;
	struct cpe_check *check = NULL;
	struct cpe_reference *ref = NULL;
	char *data;

	__attribute__nonnull__(reader);

	if (!xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CPE_ITEM_STR) &&
	    xmlTextReaderNodeType(reader) == XML_READER_TYPE_ELEMENT) {

		// we are on "<cpe-item>" element, let's alloc structure
		ret = cpe_item_new();
		if (ret == NULL)
			return NULL;

		ret->xml.lang = (char *)xmlTextReaderConstXmlLang(reader);
		ret->xml.nspace = (char *)xmlTextReaderPrefix(reader);

		// Get a name attribute of cpe-item
		data = (char *)xmlTextReaderGetAttribute(reader, ATTR_NAME_STR);
		if (data != NULL)
			ret->name = cpe_name_new(data);
		oscap_free(data);

		// if there is "deprecated", "deprecated_by" and "deprecation_date" in cpe-item element
		// ************************************************************************************
		data = (char *)xmlTextReaderGetAttribute(reader, ATTR_DEPRECATED_STR);
		if (data != NULL) {	// we have a deprecation here !
			oscap_free(data);
			data = (char *)xmlTextReaderGetAttribute(reader, ATTR_DEPRECATED_BY_STR);
			if (data == NULL || (ret->deprecated = cpe_name_new(data)) == NULL) {
				oscap_free(ret);
				oscap_free(data);
				return NULL;
			}
			oscap_free(data);

			data = (char *)xmlTextReaderGetAttribute(reader, ATTR_DEPRECATION_DATE_STR);
			if (data == NULL || (ret->deprecation_date = oscap_alloc(strlen(data) + 1)) == NULL) {
				oscap_free(ret);
				oscap_free(data);
				return NULL;
			}
			strcpy(ret->deprecation_date, (char *)data);
		}
		oscap_free(data);
		// ************************************************************************************

		xmlTextReaderNextElement(reader);
		// Now it's time to go deaply to cpe-item element and parse it's children
		// Do while there is another cpe-item element. Then return.
		while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CPE_ITEM_STR) != 0) {

			if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
				xmlTextReaderNextNode(reader);
				continue;
			}

			if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_TITLE_STR) == 0) {
				title = oscap_title_parse(reader, (char *)TAG_TITLE_STR);
				if (title)
					oscap_list_add(ret->titles, title);
			} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_NOTE_STR) == 0) {
				// it's OK to use title varible here, because note is the same structure,
				// not the reason to make a new same one
				title = oscap_title_parse(reader, (char *)TAG_NOTE_STR);
				if (title)
					oscap_list_add(ret->notes, title);
			} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CHECK_STR) == 0) {
				check = cpe_check_parse(reader);
				if (check)
					oscap_list_add(ret->checks, check);
			} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REFERENCE_STR) == 0) {
				ref = cpe_reference_parse(reader);
				if (ref)
					oscap_list_add(ret->references, ref);
				if (ref)
					printf("ref: %s\n", ref->href);
			} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_ITEM_METADATA_STR) == 0) {
				data = (char *)xmlTextReaderGetAttribute(reader, ATTR_MODIFICATION_DATE_STR);
				if ((data == NULL) || ((ret->metadata = cpe_item_metadata_new()) == NULL)) {
					cpe_item_free(ret);
					oscap_free(data);
					return NULL;
				}
				ret->metadata->modification_date = data;
				ret->metadata->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
				ret->metadata->xml.nspace = (char *)xmlTextReaderPrefix(reader);

				data = (char *)xmlTextReaderGetAttribute(reader, ATTR_STATUS_STR);
				if (data)
					ret->metadata->status = data;
				data = (char *)xmlTextReaderGetAttribute(reader, ATTR_NVD_ID_STR);
				if (data)
					ret->metadata->nvd_id = (char *)data;
				data = (char *)xmlTextReaderGetAttribute(reader, ATTR_DEP_BY_NVDID_STR);
				if (data)
					ret->metadata->deprecated_by_nvd_id = (char *)data;
				else
					ret->metadata->deprecated_by_nvd_id = NULL;
				data = NULL;

			} else
			    if ((xmlStrcmp(xmlTextReaderConstLocalName(reader),
					   TAG_REFERENCES_STR) == 0) ||
				(xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_NOTES_STR) == 0)) {
				// we just need to jump over this element

			} else {
				return ret;	// <-- we need to return here, because we don't want to jump to next element 
			}
			xmlTextReaderNextElement(reader);
		}
	}

	return ret;
}

static struct cpe_check *cpe_check_parse(xmlTextReaderPtr reader)
{

	struct cpe_check *ret;

	__attribute__nonnull__(reader);

	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_CHECK_STR) != 0)
		return NULL;

	if ((ret = oscap_alloc(sizeof(struct cpe_check))) == NULL)
		return NULL;
	memset(ret, 0, sizeof(struct cpe_check));

	ret->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
	ret->xml.nspace = (char *)xmlTextReaderPrefix(reader);
	ret->system = (char *)xmlTextReaderGetAttribute(reader, ATTR_SYSTEM_STR);
	ret->href = (char *)xmlTextReaderGetAttribute(reader, ATTR_HREF_STR);
	ret->identifier = oscap_trim((char *)xmlTextReaderReadString(reader));

	return ret;
}

static struct cpe_reference *cpe_reference_parse(xmlTextReaderPtr reader)
{

	struct cpe_reference *ret;

	__attribute__nonnull__(reader);

	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_REFERENCE_STR) != 0)
		return NULL;

	if ((ret = oscap_alloc(sizeof(struct cpe_reference))) == NULL)
		return NULL;
	memset(ret, 0, sizeof(struct cpe_reference));

	ret->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
	ret->xml.nspace = (char *)xmlTextReaderPrefix(reader);
	ret->href = (char *)xmlTextReaderGetAttribute(reader, ATTR_HREF_STR);
	ret->content = oscap_trim((char *)xmlTextReaderReadString(reader));

	return ret;
}

struct cpe_vendor *cpe_vendor_parse(xmlTextReaderPtr reader)
{

	struct cpe_vendor *ret = NULL;
	struct oscap_title *title = NULL;
	struct cpe_product *product = NULL;
	struct cpe_version *version = NULL;
	struct cpe_update *update = NULL;
	struct cpe_edition *edition = NULL;
	struct cpe_language *language = NULL;
	char *data;

	__attribute__nonnull__(reader);

	if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VENDOR_STR) != 0)
		return NULL;

	ret = cpe_vendor_new();
	if (ret == NULL)
		return NULL;

	ret->xml.nspace = (char *)xmlTextReaderPrefix(reader);
	ret->value = (char *)xmlTextReaderGetAttribute(reader, ATTR_VALUE_STR);
	// jump to next element (which should be product)
	xmlTextReaderNextElement(reader);

	while (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VENDOR_STR) != 0) {

		if (xmlTextReaderNodeType(reader) != XML_READER_TYPE_ELEMENT) {
			xmlTextReaderNextNode(reader);
			continue;
		}

		if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_TITLE_STR) == 0) {
			title = oscap_title_parse(reader, (char *)TAG_TITLE_STR);
			if (title)
				oscap_list_add(ret->titles, title);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_PRODUCT_STR) == 0) {
			// initialization
			product = cpe_product_new();
			product->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
			product->xml.nspace = (char *)xmlTextReaderPrefix(reader);
			product->value = (char *)xmlTextReaderGetAttribute(reader, ATTR_VALUE_STR);

			data = (char *)xmlTextReaderGetAttribute(reader, ATTR_PART_STR);
			if (data) {
				if (strcasecmp((const char *)data, "h") == 0)
					product->part = CPE_PART_HW;
				else if (strcasecmp((const char *)data, "o") == 0)
					product->part = CPE_PART_OS;
				else if (strcasecmp((const char *)data, "a") == 0)
					product->part = CPE_PART_APP;
				else {
					oscap_free(ret);
					oscap_free(data);
					return NULL;
				}
			} else {
				product->part = CPE_PART_NONE;
			}
			oscap_free(data);

			if (product)
				oscap_list_add(ret->products, product);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_VERSION_STR) == 0) {
			// initialization
			version = cpe_version_new();
			version->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
			version->xml.nspace = (char *)xmlTextReaderPrefix(reader);
			version->value = (char *)xmlTextReaderGetAttribute(reader, ATTR_VALUE_STR);
			oscap_list_add(product->versions, version);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_UPDATE_STR) == 0) {
			// initialization
			update = cpe_update_new();
			update->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
			update->xml.nspace = (char *)xmlTextReaderPrefix(reader);
			update->value = (char *)xmlTextReaderGetAttribute(reader, ATTR_VALUE_STR);
			oscap_list_add(version->updates, update);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_EDITION_STR) == 0) {
			// initialization
			edition = cpe_edition_new();
			edition->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
			edition->xml.nspace = (char *)xmlTextReaderPrefix(reader);
			edition->value = (char *)xmlTextReaderGetAttribute(reader, ATTR_VALUE_STR);
			oscap_list_add(update->editions, edition);
		} else if (xmlStrcmp(xmlTextReaderConstLocalName(reader), TAG_LANGUAGE_STR) == 0) {
			// initialization
			language = cpe_language_new();
			language->xml.lang = oscap_strdup((char *)xmlTextReaderConstXmlLang(reader));
			language->xml.nspace = (char *)xmlTextReaderPrefix(reader);
			language->value = (char *)xmlTextReaderGetAttribute(reader, ATTR_VALUE_STR);
			oscap_list_add(edition->languages, language);
		}
		xmlTextReaderNextNode(reader);
	}
	return ret;

}

/* End of private parsing functions
 * */
/***************************************************************************/

/***************************************************************************/
/* Private exporting functions cpe_*<structure>*_export( xmlTextWriterPtr )
 * More info in representive header file.
 * returns the type of <structure>
 */
void cpe_dict_model_export_xml(const struct cpe_dict_model *dict, const struct oscap_export_target *target)
{

	__attribute__nonnull__(dict);
	__attribute__nonnull__(target);

	// TODO: add macro to check return value from xmlTextWriter* functions
	xmlTextWriterPtr writer;

	writer = xmlNewTextWriterFilename(oscap_export_target_get_name(target), 0);
	if (writer == NULL) {
		oscap_setxmlerr(xmlGetLastError());
		return;
	}
	// Set properties of writer TODO: make public function to edit this ??
	// Yes - there will be structure oscap_export_target & oscap_parse_target
	xmlTextWriterSetIndent(writer, oscap_export_target_get_indent(target));
	xmlTextWriterSetIndentString(writer, BAD_CAST oscap_export_target_get_indent_string(target));

	if (xmlFindCharEncodingHandler(oscap_export_target_get_encoding(target)) == NULL)
		// forced default encoding
		xmlTextWriterStartDocument(writer, NULL, NULL, NULL);
	else
		xmlTextWriterStartDocument(writer, NULL, oscap_export_target_get_encoding(target), NULL);

	cpe_dict_export(dict, writer);
	xmlTextWriterEndDocument(writer);
	xmlFreeTextWriter(writer);
	if (xmlGetLastError() != NULL)
		oscap_setxmlerr(xmlGetLastError());
}

void cpe_dict_export(const struct cpe_dict_model *dict, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(dict);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST dict->xml.nspace, TAG_CPE_LIST_STR, NULL);

	OSCAP_FOREACH(xml_metadata, xml, cpe_dict_model_get_xmlns(dict),
		      if (xml->URI != NULL) xmlTextWriterWriteAttribute(writer, BAD_CAST xml->nspace,
									BAD_CAST xml->URI);)

		if (dict->generator != NULL) {
			cpe_generator_export(dict->generator, writer);
		}
	OSCAP_FOREACH(cpe_item, item, cpe_dict_model_get_items(dict),
		      // dump its contents to XML tree
		      cpe_item_export(item, writer);)
	    // TODO: NEED TO HAVE COMPONENT-TREE STRUCTURE TO GET XML-NAMESPACE 
	    xmlTextWriterStartElementNS(writer, NS_META_STR, TAG_COMPONENT_TREE_STR, NULL);
	OSCAP_FOREACH(cpe_vendor, vendor, cpe_dict_model_get_vendors(dict), cpe_vendor_export(vendor, writer);)
	    xmlTextWriterEndElement(writer);	//</component-tree>

	xmlTextWriterEndElement(writer);
	if (xmlGetLastError() != NULL)
		oscap_setxmlerr(xmlGetLastError());
}

void cpe_generator_export(const struct cpe_generator *generator, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(generator);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST generator->xml.nspace, TAG_GENERATOR_STR, NULL);
	if (generator->product_name != NULL) {
		xmlTextWriterStartElementNS(writer, BAD_CAST generator->xml.nspace, TAG_PRODUCT_NAME_STR, NULL);
		xmlTextWriterWriteString(writer, BAD_CAST generator->product_name);
		xmlTextWriterEndElement(writer);
	}
	if (generator->product_version != NULL) {
		xmlTextWriterStartElementNS(writer, BAD_CAST generator->xml.nspace, TAG_PRODUCT_VERSION_STR, NULL);
		xmlTextWriterWriteString(writer, BAD_CAST generator->product_version);
		xmlTextWriterEndElement(writer);
	}
	if (generator->schema_version != NULL) {
		xmlTextWriterStartElementNS(writer, BAD_CAST generator->xml.nspace, TAG_SCHEMA_VERSION_STR, NULL);
		xmlTextWriterWriteString(writer, BAD_CAST generator->schema_version);
		xmlTextWriterEndElement(writer);
	}
	if (generator->timestamp != NULL) {
		xmlTextWriterStartElementNS(writer, BAD_CAST generator->xml.nspace, TAG_TIMESTAMP_STR, NULL);
		xmlTextWriterWriteString(writer, BAD_CAST generator->timestamp);
		xmlTextWriterEndElement(writer);
	}
	xmlTextWriterEndElement(writer);	//</gnerator>
	if (xmlGetLastError() != NULL)
		oscap_setxmlerr(xmlGetLastError());

}

void cpe_item_export(const struct cpe_item *item, xmlTextWriterPtr writer)
{

	char *temp;
	struct oscap_iterator *it;;

	__attribute__nonnull__(item);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST item->xml.nspace, TAG_CPE_ITEM_STR, NULL);
	if (item->name != NULL) {
		temp = cpe_name_get_uri(item->name);
		xmlTextWriterWriteAttribute(writer, ATTR_NAME_STR, BAD_CAST temp);
		oscap_free(temp);
	}
	if (item->deprecated != NULL) {
		temp = cpe_name_get_uri(item->deprecated);
		xmlTextWriterWriteAttribute(writer, ATTR_DEPRECATED_STR, VAL_TRUE_STR);
		xmlTextWriterWriteAttribute(writer, ATTR_DEPRECATION_DATE_STR, BAD_CAST item->deprecation_date);
		xmlTextWriterWriteAttribute(writer, ATTR_DEPRECATED_BY_STR, BAD_CAST temp);
		oscap_free(temp);
	}

	OSCAP_FOREACH(oscap_title, title, cpe_item_get_titles(item), oscap_title_export(title, writer);)

	    if (item->metadata != NULL) {
		xmlTextWriterStartElementNS(writer, BAD_CAST item->metadata->xml.nspace, TAG_ITEM_METADATA_STR,
					    NULL);
		if (item->metadata->modification_date != NULL)
			xmlTextWriterWriteAttribute(writer, ATTR_MODIFICATION_DATE_STR,
						    BAD_CAST item->metadata->modification_date);
		if (item->metadata->status != NULL)
			xmlTextWriterWriteAttribute(writer, ATTR_STATUS_STR, BAD_CAST item->metadata->status);
		if (item->metadata->nvd_id != NULL)
			xmlTextWriterWriteAttribute(writer, ATTR_NVD_ID_STR, BAD_CAST item->metadata->nvd_id);
		if (item->metadata->deprecated_by_nvd_id != NULL)
			xmlTextWriterWriteAttribute(writer, ATTR_DEP_BY_NVDID_STR,
						    BAD_CAST item->metadata->deprecated_by_nvd_id);
		xmlTextWriterEndElement(writer);
	}

	it = oscap_iterator_new(item->references);
	if (oscap_iterator_has_more(it)) {
		xmlTextWriterStartElementNS(writer, NULL, TAG_REFERENCES_STR, NULL);
		OSCAP_FOREACH(cpe_reference, ref, cpe_item_get_references(item), cpe_reference_export(ref, writer);)
		    xmlTextWriterEndElement(writer);
	}
	oscap_iterator_free(it);

	it = oscap_iterator_new(item->notes);
	if (oscap_iterator_has_more(it)) {
		xmlTextWriterStartElementNS(writer, NULL, TAG_NOTES_STR, NULL);
		OSCAP_FOREACH(oscap_title, note, cpe_item_get_notes(item), cpe_note_export(note, writer);)
		    xmlTextWriterEndElement(writer);
	}
	oscap_iterator_free(it);

	OSCAP_FOREACH(cpe_check, check, cpe_item_get_checks(item), cpe_check_export(check, writer);)

	    xmlTextWriterEndElement(writer);
	if (xmlGetLastError() != NULL)
		oscap_setxmlerr(xmlGetLastError());

}

void cpe_vendor_export(const struct cpe_vendor *vendor, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(vendor);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST vendor->xml.nspace, TAG_VENDOR_STR, NULL);
	if (vendor->value != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_VALUE_STR, BAD_CAST vendor->value);

	OSCAP_FOREACH(oscap_title, title, cpe_vendor_get_titles(vendor), oscap_title_export(title, writer);)

	    OSCAP_FOREACH(cpe_product, product, cpe_vendor_get_products(vendor), cpe_product_export(product, writer);)

	    xmlTextWriterEndElement(writer);
	if (xmlGetLastError() != NULL)
		oscap_setxmlerr(xmlGetLastError());
}

static void cpe_product_export(const struct cpe_product *product, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(product);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST product->xml.nspace, TAG_PRODUCT_STR, NULL);
	if (product->value != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_VALUE_STR, BAD_CAST product->value);
	if (product->part != CPE_PART_NONE)
		xmlTextWriterWriteAttribute(writer, ATTR_PART_STR, BAD_CAST PART_TO_CHAR[product->part]);

	OSCAP_FOREACH(cpe_version, version, cpe_product_get_versions(product), cpe_version_export(version, writer);)

	    xmlTextWriterEndElement(writer);
}

static void cpe_version_export(const struct cpe_version *version, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(version);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST version->xml.nspace, TAG_VERSION_STR, NULL);
	if (version->value != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_VALUE_STR, BAD_CAST version->value);

	OSCAP_FOREACH(cpe_update, update, cpe_version_get_updates(version), cpe_update_export(update, writer);)

	    xmlTextWriterEndElement(writer);
	if (xmlGetLastError() != NULL)
		oscap_setxmlerr(xmlGetLastError());

}

static void cpe_update_export(const struct cpe_update *update, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(update);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST update->xml.nspace, TAG_UPDATE_STR, NULL);
	if (update->value != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_VALUE_STR, BAD_CAST update->value);

	OSCAP_FOREACH(cpe_edition, edition, cpe_update_get_editions(update), cpe_edition_export(edition, writer);)

	    xmlTextWriterEndElement(writer);
}

static void cpe_edition_export(const struct cpe_edition *edition, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(edition);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST edition->xml.nspace, TAG_EDITION_STR, NULL);
	if (edition->value != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_VALUE_STR, BAD_CAST edition->value);

	OSCAP_FOREACH(cpe_language, language, cpe_edition_get_languages(edition),
		      cpe_language_export(language, writer);)

	    xmlTextWriterEndElement(writer);
}

static void cpe_language_export(const struct cpe_language *language, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(language);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST language->xml.nspace, TAG_LANGUAGE_STR, NULL);
	if (language->value != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_VALUE_STR, BAD_CAST language->value);
	if (language->xml.lang != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_XML_LANG_STR, BAD_CAST language->xml.lang);

	xmlTextWriterEndElement(writer);
}

static void cpe_note_export(const struct oscap_title *title, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(title);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST title->xml.nspace, TAG_NOTE_STR, NULL);
	if (title->xml.lang != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_XML_LANG_STR, BAD_CAST title->xml.lang);
	if (title->content != NULL)
		xmlTextWriterWriteString(writer, BAD_CAST title->content);
	xmlTextWriterEndElement(writer);
}

static void cpe_check_export(const struct cpe_check *check, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(check);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST check->xml.nspace, TAG_CHECK_STR, NULL);
	if (check->system != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_SYSTEM_STR, BAD_CAST check->system);
	if (check->href != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_HREF_STR, BAD_CAST check->href);
	if (check->identifier != NULL)
		xmlTextWriterWriteString(writer, BAD_CAST check->identifier);
	xmlTextWriterEndElement(writer);
}

static void cpe_reference_export(const struct cpe_reference *ref, xmlTextWriterPtr writer)
{

	__attribute__nonnull__(ref);
	__attribute__nonnull__(writer);

	xmlTextWriterStartElementNS(writer, BAD_CAST ref->xml.nspace, TAG_REFERENCE_STR, NULL);
	if (ref->href != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_HREF_STR, BAD_CAST ref->href);
	if (ref->content != NULL)
		xmlTextWriterWriteString(writer, BAD_CAST ref->content);
	if (ref->xml.lang != NULL)
		xmlTextWriterWriteAttribute(writer, ATTR_XML_LANG_STR, BAD_CAST ref->xml.lang);
	xmlTextWriterEndElement(writer);
}

/* End of private export functions
 * */
/***************************************************************************/

/***************************************************************************/
/* Free functions - all are static private, do not use them outside this file
 */
void cpe_dict_model_free(struct cpe_dict_model *dict)
{

	if (dict == NULL)
		return;

	oscap_list_free(dict->items, (oscap_destruct_func) cpe_item_free);
	oscap_list_free(dict->vendors, (oscap_destruct_func) cpe_vendor_free);
	oscap_list_free(dict->xmlns, (oscap_destruct_func) xml_metadata_free);
	cpe_generator_free(dict->generator);
	xml_metadata_free(&dict->xml);
	oscap_free(dict);
}

void cpe_item_free(struct cpe_item *item)
{

	if (item == NULL)
		return;
	cpe_name_free(item->name);
	cpe_name_free(item->deprecated);
	oscap_free(item->deprecation_date);
	oscap_list_free(item->references, (oscap_destruct_func) cpe_reference_free);
	oscap_list_free(item->checks, (oscap_destruct_func) cpe_check_free);
	oscap_list_free(item->notes, (oscap_destruct_func) oscap_title_free);
	oscap_list_free(item->titles, (oscap_destruct_func) oscap_title_free);
	cpe_itemmetadata_free(item->metadata);
	xml_metadata_free(&item->xml);
	oscap_free(item);
}

void cpe_generator_free(struct cpe_generator *generator)
{

	if (generator == NULL)
		return;

	oscap_free(generator->product_name);
	oscap_free(generator->product_version);
	oscap_free(generator->schema_version);
	oscap_free(generator->timestamp);
	xml_metadata_free(&generator->xml);
	oscap_free(generator);
}

void cpe_check_free(struct cpe_check *check)
{

	if (check == NULL)
		return;

	oscap_free(check->identifier);
	oscap_free(check->system);
	oscap_free(check->href);
	xml_metadata_free(&check->xml);
	oscap_free(check);
}

void cpe_reference_free(struct cpe_reference *ref)
{

	if (ref == NULL)
		return;

	oscap_free(ref->href);
	oscap_free(ref->content);
	xml_metadata_free(&ref->xml);
	oscap_free(ref);
}

void cpe_vendor_free(struct cpe_vendor *vendor)
{

	if (vendor == NULL)
		return;

	oscap_free(vendor->value);
	oscap_list_free(vendor->titles, (oscap_destruct_func) oscap_title_free);
	oscap_list_free(vendor->products, (oscap_destruct_func) cpe_product_free);
	xml_metadata_free(&vendor->xml);
	oscap_free(vendor);
}

void cpe_product_free(struct cpe_product *product)
{

	if (product == NULL)
		return;

	oscap_free(product->value);
	oscap_list_free(product->versions, (oscap_destruct_func) cpe_version_free);
	xml_metadata_free(&product->xml);
	oscap_free(product);
}

void cpe_version_free(struct cpe_version *version)
{

	if (version == NULL)
		return;

	oscap_free(version->value);
	oscap_list_free(version->updates, (oscap_destruct_func) cpe_update_free);
	xml_metadata_free(&version->xml);
	oscap_free(version);
}

void cpe_update_free(struct cpe_update *update)
{

	if (update == NULL)
		return;

	oscap_free(update->value);
	oscap_list_free(update->editions, (oscap_destruct_func) cpe_edition_free);
	xml_metadata_free(&update->xml);
	oscap_free(update);
}

void cpe_edition_free(struct cpe_edition *edition)
{

	if (edition == NULL)
		return;

	oscap_free(edition->value);
	oscap_list_free(edition->languages, (oscap_destruct_func) cpe_language_free);
	xml_metadata_free(&edition->xml);
	oscap_free(edition);
}

void cpe_language_free(struct cpe_language *language)
{

	if (language == NULL)
		return;

	oscap_free(language->value);
	xml_metadata_free(&language->xml);
	oscap_free(language);
}

void cpe_itemmetadata_free(struct cpe_item_metadata *meta)
{

	if (meta == NULL)
		return;

	oscap_free(meta->modification_date);
	oscap_free(meta->status);
	oscap_free(meta->nvd_id);
	oscap_free(meta->deprecated_by_nvd_id);
	xml_metadata_free(&meta->xml);
	oscap_free(meta);
}

/* End of free functions
 * */
/***************************************************************************/
