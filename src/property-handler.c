/*  Copyright (C) 2009 Itsme S.r.L.
 *  Copyright (C) 2012 Roberto Guido <roberto.guido@linux.it>
 *
 *  This file is part of FSter
 *
 *  FSter is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "property-handler.h"
#include "hierarchy.h"
#include "utils.h"

#define XSD_PREFIX      "http://www.w3.org/2001/XMLSchema#"
#define XSD_BOOLEAN     XSD_PREFIX "boolean"
#define XSD_DATE        XSD_PREFIX "date"
#define XSD_DATETIME    XSD_PREFIX "dateTime"
#define XSD_DOUBLE      XSD_PREFIX "double"
#define XSD_INTEGER     XSD_PREFIX "integer"
#define XSD_STRING      XSD_PREFIX "string"
#define XSD_CLASS       "http://www.w3.org/2000/01/rdf-schema#Class"

static GHashTable   *namespaces     = NULL;
static GHashTable   *properties     = NULL;

void properties_pool_init ()
{
    gchar *query;
    gchar *uri;
    gchar *prefix;
    GVariant *response;
    GVariantIter *iter;
    GVariantIter *subiter;
    GError *error;

    if (namespaces != NULL) {
        g_warning ("Properties pool is already inited.");
        return;
    }

    namespaces = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    query = g_strdup_printf ("SELECT ?s ?prefix WHERE { ?s a tracker:Namespace . ?s tracker:prefix ?prefix }");
    error = NULL;
    response = execute_query (query, &error);
    g_free (query);

    if (response == NULL) {
        g_warning ("Unable to fetch namespaces: %s", error->message);
        g_error_free (error);
        return;
    }

    iter = NULL;
    subiter = NULL;
    g_variant_get (response, "(aas)", &iter);

    while (g_variant_iter_loop (iter, "as", &subiter)) {
        prefix = NULL;
        uri = NULL;

        if (g_variant_iter_loop (subiter, "s", &uri) && g_variant_iter_loop (subiter, "s", &prefix)) {
            printf ("%s - %s\n", prefix, uri);
            g_hash_table_insert (namespaces, g_strdup (prefix), g_strdup (uri));
        }
    }

    g_variant_unref (response);
}

void properties_pool_finish ()
{
    g_hash_table_destroy (namespaces);
    g_hash_table_destroy (properties);
    namespaces = NULL;
}

static gchar* name_to_uri (gchar *name)
{
    gchar *sep;
    const gchar *uri;
    gchar *namedup;

    namedup = strdupa (name);

    sep = strchr (namedup, ':');
    if (sep == NULL) {
        g_warning ("Unable to parse prefixed predicate name '%s'", name);
        return NULL;
    }

    *sep = '\0';

    uri = (const gchar*) g_hash_table_lookup (namespaces, namedup);

    if (uri != NULL) {
        return g_strdup_printf ("%s%s", uri, sep + 1);
    }
    else {
        g_warning ("Unable to retrieve predicate name in ontology: '%s'", name);
        return NULL;
    }
}

static gchar* uri_to_name (gchar *uri)
{
    gchar *name;
    gchar *namespace;
    GList *prefixes;
    GList *iter;

    prefixes = g_hash_table_get_keys (namespaces);
    name = NULL;

    for (iter = prefixes; iter; iter = g_list_next (iter)) {
        namespace = g_hash_table_lookup (namespaces, iter->data);
        if (g_str_has_prefix (uri, namespace)) {
            name = iter->data;
            break;
        }
    }

    g_list_free (prefixes);

    if (name != NULL) {
        return g_strdup_printf ("%s:%s", name, uri + strlen (namespace));
    }
    else {
        g_warning ("Unable to retrieve uri in ontology: '%s'", uri);
        return NULL;
    }
}

Property* properties_pool_get_by_name (gchar *name)
{
    gchar *uri;
    Property *ret;

    uri = name_to_uri (name);
    if (uri == NULL)
        return NULL;

    ret = properties_pool_get_by_uri (uri);
    g_free (uri);
    return ret;
}

static Property* fetch_property (gchar *uri)
{
    gchar *query;
    gchar *name;
    gchar *type;
    GVariant *response;
    GVariantIter *iter;
    GVariantIter *subiter;
    GError *error;
    Property *prop;
    PROPERTY_DATATYPE data_type;

    error = NULL;
    query = g_strdup_printf ("SELECT ?range WHERE { <%s> rdfs:range ?range }", uri);
    response = execute_query (query, &error);
    g_free (query);

    if (response == NULL) {
        g_warning ("Unable to retrieve property %s: %s", uri, error->message);
        g_error_free (error);
        return NULL;
    }

    prop = property_new ();

    name = uri_to_name (uri);
    property_set_name (prop, name);
    g_free (name);

    property_set_uri (prop, uri);

    iter = NULL;
    subiter = NULL;
    g_variant_get (response, "(aas)", &iter);

    if (g_variant_iter_loop (iter, "as", &subiter) && g_variant_iter_loop (subiter, "s", &type)) {
        if (strcmp (type, XSD_STRING) == 0)
            data_type = PROPERTY_TYPE_STRING;
        else if (strcmp (type, XSD_BOOLEAN) == 0)
            data_type = PROPERTY_TYPE_BOOLEAN;
        else if (strcmp (type, XSD_INTEGER) == 0)
            data_type = PROPERTY_TYPE_INTEGER;
        else if (strcmp (type, XSD_DOUBLE) == 0)
            data_type = PROPERTY_TYPE_DOUBLE;
        else if (strcmp (type, XSD_DATE) == 0)
            data_type = PROPERTY_TYPE_DATE;
        else if (strcmp (type, XSD_DATETIME) == 0)
            data_type = PROPERTY_TYPE_DATETIME;
        else if (strcmp (type, XSD_CLASS) == 0)
            data_type = PROPERTY_TYPE_CLASS;
        else
            data_type = PROPERTY_TYPE_RESOURCE;

        property_set_datatype (prop, data_type);
    }

    if (iter != NULL)
        g_variant_iter_free (iter);
    if (subiter != NULL)
        g_variant_iter_free (subiter);

    g_variant_unref (response);

    g_hash_table_insert (properties, g_strdup (uri), prop);
    return prop;
}

Property* properties_pool_get_by_uri (gchar *uri)
{
    Property *prop;

    prop = g_hash_table_lookup (properties, uri);
    if (prop == NULL)
        prop = fetch_property (uri);

    return prop;
}
