/*  Copyright (C) 2009 Itsme S.r.L.
 *
 *  This file is part of Filer
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

#define PROPERTY_HANDLER_GET_PRIVATE(obj)   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), PROPERTY_HANDLER_TYPE, PropertyHandlerPrivate))

struct _PropertyHandlerPrivate {
    gchar           *name;
    PROPERTY_TYPE   type;
};

enum {
    PROP_0,
    PROP_NAME,
};

G_DEFINE_TYPE (PropertyHandler, property_handler, G_TYPE_OBJECT);

GHashTable      *PropertiesPool         = NULL;

static void property_handler_finalize (GObject *item)
{
    PropertyHandler *ret;

    ret = PROPERTY_HANDLER (item);

    if (ret->priv->name != NULL)
        g_free (ret->priv->name);
}

static void property_handler_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    PropertyHandler *self = PROPERTY_HANDLER (object);

    switch (property_id) {
        case PROP_NAME:
            if (self->priv->name != NULL)
                g_free (self->priv->name);
            self->priv->name = g_value_dup_string (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void property_handler_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    PropertyHandler *self = PROPERTY_HANDLER (object);

    switch (property_id) {
        case PROP_NAME:
            g_value_set_string (value, self->priv->name);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void property_handler_class_init (PropertyHandlerClass *klass)
{
    GObjectClass *gobject_class;
    GParamSpec *param_spec;

    g_type_class_add_private (klass, sizeof (PropertyHandlerPrivate));

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->finalize = property_handler_finalize;
    gobject_class->set_property = property_handler_set_property;
    gobject_class->get_property = property_handler_get_property;

    param_spec = g_param_spec_string ("name",
                                        "Property's Name",
                                        "Short hand name of the property",
                                        NULL,
                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_NAME, param_spec);
}

static void property_handler_init (PropertyHandler *item)
{
    item->priv = PROPERTY_HANDLER_GET_PRIVATE (item);
    memset (item->priv, 0, sizeof (PropertyHandlerPrivate));
}

void properties_pool_init ()
{
    PropertiesPool = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

void properties_pool_finish ()
{
    g_hash_table_destroy (PropertiesPool);
}

static PROPERTY_TYPE retrieve_type_by_name (gchar *name)
{
    gchar *query;
    gchar *complete_type;
    gchar *type;
    gchar **values;
    PROPERTY_TYPE ret;
    GPtrArray *response;
    GError *error;

    error = NULL;
    query = g_strdup_printf ("SELECT ?r WHERE { %s a rdf:Property ; rdfs:domain ?d ; rdfs:range ?r }", name);
    response = tracker_resources_sparql_query (get_tracker_client (), query, &error);
    g_free (query);

    if (response == NULL) {
        error = NULL;
        query = g_strdup_printf ("SELECT ?r WHERE { ?a a rdf:Property ; rdfs:domain %s ; rdfs:range ?r }", name);
        response = tracker_resources_sparql_query (get_tracker_client (), query, &error);
        g_free (query);

        if (response == NULL) {
            g_warning ("Unable to fetch property %s datatype: %s", name, error->message);
            g_error_free (error);
            ret = PROPERTY_TYPE_UNKNOWN;
            return ret;
        }
    }

    values = (gchar**) g_ptr_array_index (response, 0);
    complete_type = values [ 0 ];
    type = strrchr (complete_type, '#') + 1;

    if (strcmp (type, "string") == 0)
        ret = PROPERTY_TYPE_STRING;
    else if (strcmp (type, "boolean") == 0)
        ret = PROPERTY_TYPE_BOOLEAN;
    else if (strcmp (type, "integer") == 0)
        ret = PROPERTY_TYPE_INTEGER;
    else if (strcmp (type, "double") == 0)
        ret = PROPERTY_TYPE_DOUBLE;
    else if (strcmp (type, "dateTime") == 0)
        ret = PROPERTY_TYPE_DATETIME;
    else
        ret = PROPERTY_TYPE_RESOURCE;

    g_ptr_array_foreach (response, (GFunc) g_strfreev, NULL);
    g_ptr_array_free (response, TRUE);

    return ret;
}

static PropertyHandler* property_handler_new (gchar *name)
{
    PropertyHandler *ret;

    ret = g_object_new (PROPERTY_HANDLER_TYPE, "name", name, NULL);
    ret->priv->type = retrieve_type_by_name (name);
    return ret;
}

PropertyHandler* properties_pool_get (gchar *name)
{
    PropertyHandler *ret;

    ret = g_hash_table_lookup (PropertiesPool, name);

    if (ret == NULL) {
        ret = property_handler_new (name);
        g_hash_table_insert (PropertiesPool, name, ret);
        g_object_ref (ret);
    }

    return ret;
}

const gchar* property_handler_get_name (PropertyHandler *property)
{
    return (const gchar*) property->priv->name;
}

PROPERTY_TYPE property_handler_get_format (PropertyHandler *property)
{
    return property->priv->type;
}

gchar* property_handler_format_value (PropertyHandler *property, const gchar *value)
{
    gchar *ret;
    GDate *d;
    struct tm tm;

    ret = NULL;

    switch (property->priv->type) {
        case PROPERTY_TYPE_STRING:
            ret = g_strdup_printf ("\"%s\"", value);
            break;

        case PROPERTY_TYPE_DATETIME:
            d = g_date_new ();
            g_date_set_parse (d, value);

            if (g_date_valid (d) == TRUE) {
                g_date_to_struct_tm (d, &tm);
                ret = g_strdup_printf ("%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            }

            g_date_free (d);
            break;

        case PROPERTY_TYPE_BOOLEAN:
        case PROPERTY_TYPE_INTEGER:
        case PROPERTY_TYPE_DOUBLE:
        case PROPERTY_TYPE_RESOURCE:
        default:
            ret = g_strdup (value);
            break;
    }

    return ret;
}
