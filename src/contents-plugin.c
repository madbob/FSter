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

#include "contents-plugin.h"

#define CONTENTS_PLUGIN_GET_PRIVATE(obj)    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), CONTENTS_PLUGIN_TYPE, ContentsPluginPrivate))

struct _ContentsPluginPrivate {
    gchar       *metadata;
};

G_DEFINE_ABSTRACT_TYPE (ContentsPlugin, contents_plugin, G_TYPE_OBJECT);

static void contents_plugin_class_init (ContentsPluginClass *klass)
{
    g_type_class_add_private (klass, sizeof (ContentsPluginPrivate));
}

static void contents_plugin_init (ContentsPlugin *item)
{
    item->priv = CONTENTS_PLUGIN_GET_PRIVATE (item);
    memset (item->priv, 0, sizeof (ContentsPluginPrivate));
}

void contents_plugin_set_metadata (ContentsPlugin *self, gchar *metadata)
{
    if (self->priv->metadata != NULL)
        g_free (self->priv->metadata);
    self->priv->metadata = g_strdup (metadata);
}

const gchar* contents_plugin_get_metadata (ContentsPlugin *self)
{
    return (const gchar*) self->priv->metadata;
}

const gchar* contents_plugin_get_name (ContentsPlugin *self)
{
	return CONTENTS_PLUGIN_GET_CLASS (self)->get_name (self);
}

gchar* contents_plugin_get_file (ContentsPlugin *self, ItemHandler *item)
{
	return CONTENTS_PLUGIN_GET_CLASS (self)->get_file (self, item);
}
