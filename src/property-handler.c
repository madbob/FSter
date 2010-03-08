/*  Copyright (C) 2009 Itsme S.r.L.
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
    register int i;
    gchar *query;
    gchar **values;
    GPtrArray *response;
    GError *error;

    if (namespaces != NULL) {
        g_warning ("Properties pool is already inited.");
        return;
    }

    namespaces = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    properties = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

    query = g_strdup_printf ("SELECT ?s ?prefix WHERE { ?s a tracker:Namespace . ?s tracker:prefix ?prefix }");
    error = NULL;
    response = tracker_resources_sparql_query (get_tracker_client (), query, &error);
    g_free (query);

    if (response == NULL) {
        g_warning ("Unable to fetch namespaces: %s", error->message);
        g_error_free (error);
        return;
    }

    for (i = 0; i < response->len; i++) {
        values = (gchar**) g_ptr_array_index (response, i);
        g_hash_table_insert (namespaces, values [1], values [0]);

        /*
            String are not freed here, are maintained in the hash table
        */

        g_free (values);
    }

    g_ptr_array_free (response, TRUE);
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
    gchar **values;
    GPtrArray *response;
    GError *error;
    Property *prop;
    PROPERTY_DATATYPE data_type;

    error = NULL;
    query = g_strdup_printf ("SELECT ?range WHERE { <%s> rdfs:range ?range }", uri);
    response = tracker_resources_sparql_query (get_tracker_client (), query, &error);
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

    values = (gchar**) g_ptr_array_index (response, 0);

    if (strcmp (values [0], XSD_STRING) == 0)
        data_type = PROPERTY_TYPE_STRING;
    else if (strcmp (values [0], XSD_BOOLEAN) == 0)
        data_type = PROPERTY_TYPE_BOOLEAN;
    else if (strcmp (values [0], XSD_INTEGER) == 0)
        data_type = PROPERTY_TYPE_INTEGER;
    else if (strcmp (values [0], XSD_DOUBLE) == 0)
        data_type = PROPERTY_TYPE_DOUBLE;
    else if (strcmp (values [0], XSD_DATE) == 0)
        data_type = PROPERTY_TYPE_DATE;
    else if (strcmp (values [0], XSD_DATETIME) == 0)
        data_type = PROPERTY_TYPE_DATETIME;
    else if (strcmp (values [0], XSD_CLASS) == 0)
        data_type = PROPERTY_TYPE_CLASS;
    else
        data_type = PROPERTY_TYPE_RESOURCE;

    g_ptr_array_foreach (response, (GFunc) g_strfreev, NULL);
    g_ptr_array_free (response, TRUE);

    property_set_datatype (prop, data_type);

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
