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

#ifndef CONTENTS_REAL_PLUGIN_H
#define CONTENTS_REAL_PLUGIN_H

#include "common.h"
#include "contents-plugin.h"

#define CONTENTS_REAL_PLUGIN_TYPE               (contents_real_plugin_get_type ())
#define CONTENTS_REAL_PLUGIN(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj),       \
                                                 CONTENTS_REAL_PLUGIN, ContentsRealPlugin))
#define CONTENTS_REAL_PLUGIN_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass),        \
                                                 CONTENTS_REAL_PLUGIN,                    \
                                                 ContentsRealPluginClass))
#define IS_CONTENTS_REAL_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj),       \
                                                 CONTENTS_REAL_PLUGIN_TYPE))
#define IS_CONTENTS_REAL_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass),        \
                                                 CONTENTS_REAL_PLUGIN_TYPE))
#define CONTENTS_REAL_PLUGIN_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj),        \
                                                 CONTENTS_REAL_PLUGIN_TYPE,               \
                                                 ContentsRealPluginClass))

typedef struct _ContentsRealPlugin         ContentsRealPlugin;
typedef struct _ContentsRealPluginClass    ContentsRealPluginClass;

struct _ContentsRealPlugin {
    ContentsPlugin		        parent;
};

struct _ContentsRealPluginClass {
    ContentsPluginClass         parent_class;
};

GType           contents_real_plugin_get_type           ();

#endif
