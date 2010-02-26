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

#ifndef PROPERTY_H
#define PROPERTY_H

#include "common.h"

#define PROPERTY_TYPE             (property_get_type ())
#define PROPERTY(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),   \
                                   PROPERTY_TYPE, Property))
#define PROPERTY_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),    \
                                   PROPERTY_TYPE,                       \
                                   PropertyClass))
#define IS_PROPERTY(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),   \
                                   PROPERTY_TYPE))
#define IS_PROPERTY_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),    \
                                   PROPERTY_TYPE))
#define PROPERTY_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),    \
                                   PROPERTY_TYPE,                       \
                                   PropertyClass))

typedef enum {
    PROPERTY_TYPE_UNKNOWN,
    PROPERTY_TYPE_STRING,
    PROPERTY_TYPE_BOOLEAN,
    PROPERTY_TYPE_INTEGER,
    PROPERTY_TYPE_DOUBLE,
    PROPERTY_TYPE_DATE,
    PROPERTY_TYPE_DATETIME,
    PROPERTY_TYPE_RESOURCE,
    PROPERTY_TYPE_CLASS,
} PROPERTY_DATATYPE;

typedef struct _Property         Property;
typedef struct _PropertyClass    PropertyClass;
typedef struct _PropertyPrivate  PropertyPrivate;

struct _Property {
    GObject         parent;
    PropertyPrivate *priv;
};

struct _PropertyClass {
    GObjectClass    parent_class;
};

GType               property_get_type       ();
Property*           property_new            ();
void                property_set_name       (Property *property, gchar *name);
const gchar*        property_get_name       (Property *property);
void                property_set_uri        (Property *property, gchar *uri);
const gchar*        property_get_uri        (Property *property);
void                property_set_datatype   (Property *property, PROPERTY_DATATYPE type);
PROPERTY_DATATYPE   property_get_datatype   (Property *property);

gchar*              property_format_value   (Property *property, const gchar *value);

#endif
