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

#include "property.h"

#define PROPERTY_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), PROPERTY_TYPE, PropertyPrivate))

struct _PropertyPrivate {
    gchar               *name;
    gchar               *uri;
    PROPERTY_DATATYPE   type;
};

G_DEFINE_TYPE (Property, property, G_TYPE_OBJECT);

static void property_finalize (GObject *item)
{
    Property *ret;

    ret = PROPERTY (item);

    if (ret->priv->name != NULL)
        g_free (ret->priv->name);
    if (ret->priv->uri != NULL)
        g_free (ret->priv->uri);
}

static void property_class_init (PropertyClass *klass)
{
    GObjectClass *gobject_class;

    g_type_class_add_private (klass, sizeof (PropertyPrivate));

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->finalize = property_finalize;
}

static void property_init (Property *item)
{
    item->priv = PROPERTY_GET_PRIVATE (item);
    memset (item->priv, 0, sizeof (PropertyPrivate));
}

Property* property_new ()
{
    return g_object_new (PROPERTY_TYPE, NULL);
}

void property_set_name (Property *property, gchar *name)
{
    if (property->priv->name != NULL)
        g_free (property->priv->name);
    property->priv->name = g_strdup (name);
}

const gchar* property_get_name (Property *property)
{
    return (const gchar*) property->priv->name;
}

void property_set_uri (Property *property, gchar *uri)
{
    if (property->priv->uri != NULL)
        g_free (property->priv->uri);
    property->priv->uri = g_strdup (uri);
}

const gchar* property_get_uri (Property *property)
{
    return (const gchar*) property->priv->uri;
}

void property_set_datatype (Property *property, PROPERTY_DATATYPE type)
{
    property->priv->type = type;
}

PROPERTY_DATATYPE property_get_datatype (Property *property)
{
    return property->priv->type;
}

gchar* property_format_value (Property *property, const gchar *value)
{
    gchar *ret;
    GDate *d;
    struct tm tm;

    ret = NULL;

    switch (property_get_datatype (property)) {
        case PROPERTY_TYPE_STRING:
            ret = g_strdup_printf ("\"%s\"", value);
            break;

        case PROPERTY_TYPE_DATETIME:
            d = g_date_new ();
            g_date_set_parse (d, value);

            if (g_date_valid (d) == TRUE) {
                g_date_to_struct_tm (d, &tm);
                ret = g_strdup_printf ("\"%04d-%02d-%02dT%02d:%02d:%02dZ\"",
                                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                                       tm.tm_hour, tm.tm_min, tm.tm_sec);
            }

            g_date_free (d);
            break;

        case PROPERTY_TYPE_RESOURCE:
            ret = g_strdup_printf ("<%s>", value);
            break;

        case PROPERTY_TYPE_BOOLEAN:
        case PROPERTY_TYPE_INTEGER:
        case PROPERTY_TYPE_DOUBLE:
        default:
            ret = g_strdup (value);
            break;
    }

    return ret;
}
