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

#include "utils.h"

void easy_list_free (GList *list)
{
    GList *iter;

    for (iter = list; iter; iter = g_list_next (iter))
        g_free (iter->data);

    g_list_free (list);
}

gchar* from_glist_to_string (GList *strings, const gchar *separator, gboolean free_list)
{
    register int i;
    gchar *ret;
    gchar **array;
    GList *iter;

    array = alloca (g_list_length (strings) + 1);

    for (iter = strings, i = 0; iter; iter = g_list_next (iter), i++)
        array [i] = (gchar*) iter->data;

    array [i] = NULL;
    ret = g_strjoinv (separator, array);

    if (free_list == TRUE) {
        for (iter = strings; iter; iter = g_list_next (iter))
            g_free (iter->data);

        g_list_free (strings);
    }

    return ret;
}

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
