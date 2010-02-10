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
    int index;
    int tot_len;
    gchar *ret;
    GList *iter;

    if (strings == NULL)
        return g_strdup ("");

    tot_len = 0;

    for (iter = strings; iter; iter = g_list_next (iter))
        tot_len += strlen ((gchar*) iter->data);

    /*
        Extra-large allocation...
    */
    ret = alloca (tot_len * 2);
    index = 0;

    for (iter = strings; iter != NULL && iter->next != NULL; iter = g_list_next (iter))
        index += snprintf (ret + index, (tot_len * 2) - index, "%s%s", (gchar*) iter->data, separator);
    snprintf (ret + index, (tot_len * 2) - index, "%s", (gchar*) iter->data);

    return g_strdup (ret);
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

void create_file (gchar *path)
{
    FILE *tmp;

    tmp = fopen (path, "w");

    if (tmp != NULL)
        fclose (tmp);
    else
        g_warning ("Unable to touch new file in %s\n", path);
}
