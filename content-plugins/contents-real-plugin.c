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

#include "contents-real-plugin.h"

G_DEFINE_TYPE (ContentsRealPlugin, contents_real_plugin, CONTENTS_PLUGIN_TYPE);

static const gchar* contents_real_plugin_get_name (ContentsPlugin *self)
{
    return "real_file";
}

static gchar* contents_real_plugin_get_file (ContentsPlugin *self, ItemHandler *item)
{
    gchar *path;

    path = (gchar*) item_handler_get_metadata (item, "nie:isStoredAs");
    if (path != NULL)
        path = g_filename_from_uri (path, NULL, NULL);

    return path;
}

static void contents_real_plugin_class_init (ContentsRealPluginClass *klass)
{
	ContentsPluginClass *content_plugin_class;

    content_plugin_class = CONTENTS_PLUGIN_CLASS (klass);
    content_plugin_class->get_name = contents_real_plugin_get_name;
    content_plugin_class->get_file = contents_real_plugin_get_file;
}

static void contents_real_plugin_init (ContentsRealPlugin *item)
{
}

GType load_contents_plugin_type ()
{
    return contents_real_plugin_get_type ();
}
