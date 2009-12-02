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

#include "contents-dumpmetadata-plugin.h"

#define METADATA_DUMPS_PATH             "/tmp/.avfs_dumps/"

G_DEFINE_TYPE (ContentsDumpmetadataPlugin, contents_dumpmetadata_plugin, CONTENTS_PLUGIN_TYPE);

static const gchar* contents_dumpmetadata_plugin_get_name (ContentsPlugin *self)
{
    return "dump_metadata";
}

static gchar* contents_dumpmetadata_plugin_get_file (ContentsPlugin *self, ItemHandler *item)
{
    FILE *file;
    gchar *path;
    const gchar *id;
    const gchar *value;
    GList *metadata_list;
    GList *iter;

    id = item_handler_exposed_name (item);
    path = g_build_filename (METADATA_DUMPS_PATH, id, NULL);

    /**
        TODO    If dump file already exists it is used, but it is not updated with latest metadata
                assignments and changes. Provide some kind of check and update
    */
    if (access (path, F_OK) != 0) {
        metadata_list = item_handler_get_all_metadata (item);
        file = fopen (path, "w");

        if (file == NULL) {
            g_warning ("Error dumping metadata: unable to create file in %s, %s", path, strerror (errno));
        }
        else {
            for (iter = metadata_list; iter; iter = g_list_next (iter)) {
                value = item_handler_get_metadata (item, (gchar*) iter->data);

                if (value == NULL) {
                    g_warning ("Error dumping metadata: '%s' exists but has no value", (gchar*) iter->data);
                    continue;
                }

                fprintf (file, "%s: %s\n", (gchar*) iter->data, value);
            }

            g_list_free (metadata_list);
            fclose (file);
        }
    }

    return path;
}

static void contents_dumpmetadata_plugin_class_init (ContentsDumpmetadataPluginClass *klass)
{
    ContentsPluginClass *content_plugin_class;

    content_plugin_class = CONTENTS_PLUGIN_CLASS (klass);
    content_plugin_class->get_name = contents_dumpmetadata_plugin_get_name;
    content_plugin_class->get_file = contents_dumpmetadata_plugin_get_file;
}

static void contents_dumpmetadata_plugin_init (ContentsDumpmetadataPlugin *item)
{
}

/*
    This is duplicated from utils.c, perhaps may be moved in a more convenient sector
*/
void check_and_create_folder (gchar *path)
{
    gboolean ret;
    GFile *dummy;
    GError *error;

    dummy = g_file_new_for_path (path);

    if (g_file_query_exists (dummy, NULL) == FALSE) {
        error = NULL;
        ret = g_file_make_directory_with_parents (dummy, NULL, &error);

        if (ret == FALSE) {
            g_warning ("Error: unable to create directory %s", error->message);
            g_error_free (error);
        }
    }

    g_object_unref (dummy);
}

GType load_contents_plugin_type ()
{
    check_and_create_folder (METADATA_DUMPS_PATH);
    return contents_dumpmetadata_plugin_get_type ();
}
