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

#ifndef PROPERTY_HANDLER_H
#define PROPERTY_HANDLER_H

#include "core.h"

#define PROPERTY_HANDLER_TYPE               (property_handler_get_type ())
#define PROPERTY_HANDLER(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj),     \
                                             PROPERTY_HANDLER_TYPE, PropertyHandler))
#define PROPERTY_HANDLER_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass),      \
                                             PROPERTY_HANDLER_TYPE,                 \
                                             PropertyHandlerClass))
#define IS_PROPERTY_HANDLER(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj),     \
                                             PROPERTY_HANDLER_TYPE))
#define IS_PROPERTY_HANDLER_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass),      \
                                             PROPERTY_HANDLER_TYPE))
#define PROPERTY_HANDLER_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj),      \
                                             PROPERTY_HANDLER_TYPE,                 \
                                             PropertyHandlerClass))

typedef enum {
    PROPERTY_TYPE_UNKNOWN,
    PROPERTY_TYPE_STRING,
    PROPERTY_TYPE_BOOLEAN,
    PROPERTY_TYPE_INTEGER,
    PROPERTY_TYPE_DOUBLE,
    PROPERTY_TYPE_DATETIME,
    PROPERTY_TYPE_RESOURCE,
} PROPERTY_TYPE;

typedef struct _PropertyHandler         PropertyHandler;
typedef struct _PropertyHandlerClass    PropertyHandlerClass;
typedef struct _PropertyHandlerPrivate  PropertyHandlerPrivate;

struct _PropertyHandler {
    GObject                 parent;
    PropertyHandlerPrivate  *priv;
};

struct _PropertyHandlerClass {
    GObjectClass    parent_class;
};

GType               property_handler_get_type       ();

void                properties_pool_init            ();
void                properties_pool_finish          ();

PropertyHandler*    properties_pool_get             (gchar *name);

const gchar*        property_handler_get_name       (PropertyHandler *property);
PROPERTY_TYPE       property_handler_get_format     (PropertyHandler *property);

gchar*              property_handler_format_value   (PropertyHandler *property, const gchar *value);

#endif
