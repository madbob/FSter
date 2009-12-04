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

#ifndef CONTENTS_PLUGIN_H
#define CONTENTS_PLUGIN_H

#include "common.h"
#include "item-handler.h"

#define CONTENTS_PLUGIN_TYPE            (contents_plugin_get_type ())
#define CONTENTS_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj),	    \
                                         CONTENTS_PLUGIN_TYPE, ContentsPlugin))
#define CONTENTS_PLUGIN_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass),    	\
                                         CONTENTS_PLUGIN_TYPE,                  \
                                         ContentsPluginClass))
#define IS_CONTENTS_PLUGIN(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj),   	\
                                         CONTENTS_PLUGIN_TYPE))
#define IS_CONTENTS_PLUGIN_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),    	\
                                         CONTENTS_PLUGIN_TYPE))
#define CONTENTS_PLUGIN_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),    	\
                                         CONTENTS_PLUGIN_TYPE,                  \
                                         ContentsPluginClass))

typedef struct _ContentsPlugin         ContentsPlugin;
typedef struct _ContentsPluginClass    ContentsPluginClass;

struct _ContentsPlugin {
    GObject             	parent;
};

struct _ContentsPluginClass {
    GObjectClass    	parent_class;

    const gchar* 	(*get_name) (ContentsPlugin *self);
    gchar*          (*get_file) (ContentsPlugin *self, ItemHandler *item);
};

GType           contents_plugin_get_type        ();

const gchar*    contents_plugin_get_name        (ContentsPlugin *self);
gchar*          contents_plugin_get_file        (ContentsPlugin *self, ItemHandler *item);

#endif
