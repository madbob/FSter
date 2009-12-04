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

#ifndef CONTENTS_DUMPMETADATA_PLUGIN_H
#define CONTENTS_DUMPMETADATA_PLUGIN_H

#include "common.h"
#include "contents-plugin.h"

#define CONTENTS_DUMPMETADATA_PLUGIN_TYPE               (contents_dumpmetadata_plugin_get_type ())
#define CONTENTS_DUMPMETADATA_PLUGIN(obj)               (G_TYPE_CHECK_INSTANCE_CAST ((obj),             \
                                                         CONTENTS_DUMPMETADATA_PLUGIN, ContentsDumpmetadataPlugin))
#define CONTENTS_DUMPMETADATA_PLUGIN_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST ((klass),              \
                                                         CONTENTS_DUMPMETADATA_PLUGIN,                  \
                                                         ContentsDumpmetadataPluginClass))
#define IS_CONTENTS_DUMPMETADATA_PLUGIN(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj),             \
                                                         CONTENTS_DUMPMETADATA_PLUGIN_TYPE))
#define IS_CONTENTS_DUMPMETADATA_PLUGIN_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass),              \
                                                         CONTENTS_DUMPMETADATA_PLUGIN_TYPE))
#define CONTENTS_DUMPMETADATA_PLUGIN_GET_CLASS(obj)     (G_TYPE_INSTANCE_GET_CLASS ((obj),              \
                                                         CONTENTS_DUMPMETADATA_PLUGIN_TYPE,             \
                                                         ContentsDumpmetadataPluginClass))

typedef struct _ContentsDumpmetadataPlugin         ContentsDumpmetadataPlugin;
typedef struct _ContentsDumpmetadataPluginClass    ContentsDumpmetadataPluginClass;

struct _ContentsDumpmetadataPlugin {
    ContentsPlugin              parent;
};

struct _ContentsDumpmetadataPluginClass {
    ContentsPluginClass         parent_class;
};

GType           contents_dumpmetadata_plugin_get_type           ();

#endif
