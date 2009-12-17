/*  Copyright (C) 2009 Itsme S.r.L.
 *
 *  This file is part of FSter
 *
 *  Guglielmo is free software; you can redistribute it and/or modify
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

#define RDFS_PREFIX "http://www.w3.org/2000/01/rdf-schema#"
#define TRACKER_PREFIX "http://www.tracker-project.org/ontologies/tracker#"

#define RDF_PREFIX TRACKER_RDF_PREFIX
#define RDF_PROPERTY RDF_PREFIX "Property"
#define RDF_TYPE RDF_PREFIX "type"

#define RDFS_CLASS RDFS_PREFIX "Class"
#define RDFS_DOMAIN RDFS_PREFIX "domain"
#define RDFS_RANGE RDFS_PREFIX "range"
#define RDFS_SUB_CLASS_OF RDFS_PREFIX "subClassOf"
#define RDFS_SUB_PROPERTY_OF RDFS_PREFIX "subPropertyOf"

#define NRL_PREFIX TRACKER_NRL_PREFIX
#define NRL_INVERSE_FUNCTIONAL_PROPERTY TRACKER_NRL_PREFIX "InverseFunctionalProperty"
#define NRL_MAX_CARDINALITY NRL_PREFIX "maxCardinality"

static TrackerClass* class_get_by_uri (gchar *uri);

void properties_pool_init ()
{
    register int i;
    gchar *query;
    gchar **values;
    GPtrArray *response;
    GError *error;
    TrackerNamespace *namespace;

    tracker_ontology_init ();

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
        namespace = tracker_namespace_new ();
        tracker_namespace_set_uri (namespace, values [0]);
        tracker_namespace_set_prefix (namespace, values [1]);
        tracker_ontology_add_namespace (namespace);
        g_free (values);
    }

    g_ptr_array_free (response, TRUE);
}

void properties_pool_finish ()
{
    tracker_ontology_shutdown ();
}

static gchar* name_to_uri (gchar *name)
{
    register int i;
    guint num;
    gchar *sep;
    const gchar *uri;
    gchar *namedup;
    TrackerNamespace **namespaces;

    namedup = strdupa (name);

    sep = strchr (namedup, ':');
    if (sep == NULL) {
        g_warning ("Unable to parse prefixed predicate name '%s'", name);
        return NULL;
    }

    *sep = '\0';
    uri = NULL;
    namespaces = tracker_ontology_get_namespaces (&num);

    for (i = 0; i < num; i++) {
        if (strcmp (namedup, tracker_namespace_get_prefix (namespaces [i])) == 0) {
            uri = tracker_namespace_get_uri (namespaces [i]);
            break;
        }
    }

    if (uri != NULL)
        return g_strdup_printf ("%s%s", uri, sep + 1);

    g_warning ("Unable to retrieve predicate name in ontology: '%s'", name);
    return NULL;
}

TrackerProperty* properties_pool_get_by_name (gchar *name)
{
    gchar *uri;
    TrackerProperty *ret;

    uri = name_to_uri (name);
    if (uri == NULL)
        return NULL;

    ret = properties_pool_get_by_uri (uri);
    g_free (uri);
    return ret;
}

static void fetch_property (gchar *uri)
{
    register int i;
    gchar *query;
    gchar **values;
    gchar *subject;
    gchar *predicate;
    gchar *object;
    GPtrArray *response;
    GError *error;
    TrackerClass *class;
    TrackerClass *super_class;
    TrackerProperty *property;
    TrackerProperty *super_property;

    error = NULL;
    query = g_strdup_printf ("SELECT ?pred ?val WHERE { <%s> ?pred ?val }", uri);
    response = tracker_resources_sparql_query (get_tracker_client (), query, &error);
    g_free (query);

    if (response == NULL) {
        g_warning ("Unable to retrieve property %s: %s", uri, error->message);
        g_error_free (error);
        return;
    }

    for (i = 0; i < response->len; i++) {
        values = (gchar**) g_ptr_array_index (response, i);
        subject = uri;
        predicate = values [0];
        object = values [1];

        if (g_strcmp0 (predicate, RDF_TYPE) == 0) {
            if (g_strcmp0 (object, RDFS_CLASS) == 0) {
                class = tracker_class_new ();
                tracker_class_set_uri (class, subject);
                tracker_ontology_add_class (class);
                g_object_unref (class);
            }
            else if (g_strcmp0 (object, RDF_PROPERTY) == 0) {
                property = tracker_property_new ();
                tracker_property_set_uri (property, subject);
                tracker_ontology_add_property (property);
            }
        }
        else if (g_strcmp0 (predicate, RDFS_SUB_CLASS_OF) == 0) {
            class = class_get_by_uri (subject);
            super_class = class_get_by_uri (object);
            tracker_class_add_super_class (class, super_class);
        }
        else if (g_strcmp0 (predicate, RDFS_SUB_PROPERTY_OF) == 0) {
            property = properties_pool_get_by_uri (subject);
            super_property = properties_pool_get_by_uri (object);
            tracker_property_add_super_property (property, super_property);
        }
        else if (g_strcmp0 (predicate, RDFS_DOMAIN) == 0) {
            property = properties_pool_get_by_uri (subject);
            class = class_get_by_uri (object);
            tracker_property_set_domain (property, class);
        }
        else if (g_strcmp0 (predicate, RDFS_RANGE) == 0) {
            property = properties_pool_get_by_uri (subject);
            class = class_get_by_uri (object);
            tracker_property_set_range (property, class);
        }
        else if (g_strcmp0 (predicate, NRL_MAX_CARDINALITY) == 0) {
            if (atoi (object) == 1) {
                property = properties_pool_get_by_uri (subject);
                tracker_property_set_multiple_values (property, FALSE);
            }
        }
        else if (g_strcmp0 (predicate, TRACKER_PREFIX "isAnnotation") == 0) {
            if (g_strcmp0 (object, "true") == 0) {
                property = properties_pool_get_by_uri (subject);
                tracker_property_set_embedded (property, FALSE);
            }
        }
        else if (g_strcmp0 (predicate, TRACKER_PREFIX "fulltextIndexed") == 0) {
            if (strcmp (object, "true") == 0) {
                property = properties_pool_get_by_uri (subject);
                tracker_property_set_fulltext_indexed (property, TRUE);
            }
        }
    }
}

static TrackerClass* class_get_by_uri (gchar *uri)
{
    TrackerClass *ret;

    ret = tracker_ontology_get_class_by_uri (uri);
    if (ret == NULL) {
        fetch_property (uri);
        ret = tracker_ontology_get_class_by_uri (uri);
    }

    return ret;
}

TrackerProperty* properties_pool_get_by_uri (gchar *uri)
{
    TrackerProperty *prop;

    prop = tracker_ontology_get_property_by_uri (uri);
    if (prop == NULL) {
        fetch_property (uri);
        prop = tracker_ontology_get_property_by_uri (uri);
    }

    return prop;
}

gchar* property_handler_format_value (TrackerProperty *property, const gchar *value)
{
    gchar *ret;
    GDate *d;
    struct tm tm;

    ret = NULL;

    switch (tracker_property_get_data_type (property)) {
        case TRACKER_PROPERTY_TYPE_STRING:
            ret = g_strdup_printf ("\"%s\"", value);
            break;

        case TRACKER_PROPERTY_TYPE_DATETIME:
            d = g_date_new ();
            g_date_set_parse (d, value);

            if (g_date_valid (d) == TRUE) {
                g_date_to_struct_tm (d, &tm);
                ret = g_strdup_printf ("%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            }

            g_date_free (d);
            break;

        case TRACKER_PROPERTY_TYPE_BOOLEAN:
        case TRACKER_PROPERTY_TYPE_INTEGER:
        case TRACKER_PROPERTY_TYPE_DOUBLE:
        case TRACKER_PROPERTY_TYPE_RESOURCE:
        default:
            ret = g_strdup (value);
            break;
    }

    return ret;
}
