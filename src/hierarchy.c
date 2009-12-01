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

#include "common.h"
#include "hierarchy.h"
#include "nodes-cache.h"
#include "property-handler.h"
#include "utils.h"

static HierarchyNode                    *ExposingTree;
static NodesCache                       *Cache;

static void create_dummy_references ()
{
    gchar *path;

    path = DUMMY_FILEPATH;
    fclose (fopen (path, "w"));

    mkdir (FAKE_SAVING_FOLDER, 0770);
    mkdir (DUMMY_DIRPATH, 0770);
}

void build_hierarchy_tree_from_xml (xmlDocPtr doc)
{
    xmlNode *root;
    xmlNode *node;

    root = xmlDocGetRootElement (doc);
    if (strcmp ((gchar*) root->name, "conf") != 0) {
        g_warning ("Error: configuration file must be enclosed in a 'conf' tag, '%s' is found", root->name);
        return;
    }

    properties_pool_init ();

    for (node = root->children; node; node = node->next) {
        if (strcmp ((gchar*) node->name, "exposing_tree") == 0) {
            ExposingTree = hierarchy_node_new_from_xml (NULL, node->children);
        }
        else if (strcmp ((gchar*) node->name, "saving_tree") == 0) {
            /**
                TODO    Handle saving-tree
            */
        }
        else {
            g_warning ("Error: unrecognized tag '%s'", (gchar*) node->name);
        }
    }

    create_dummy_references ();
    Cache = nodes_cache_new ();
}

void destroy_hierarchy_tree ()
{
    g_object_unref (Cache);
    g_object_unref (ExposingTree);
    tracker_disconnect (get_tracker_client ());
    properties_pool_finish ();
}

static GList* tokenize_path (const gchar *path)
{
    gchar *token;
    gchar *tmp;
    gchar *dirs;
    GList *list;

    list = NULL;
    dirs = g_strdup (path);

    for (;;) {
        token = g_path_get_basename (dirs);
        if (token == NULL)
            break;

        list = g_list_prepend (list, token);
        tmp = g_path_get_dirname (dirs);

        if (tmp == NULL || strcmp (tmp, "/") == 0) {
            if (tmp != NULL)
                g_free (tmp);
            break;
        }

        g_free (dirs);
        dirs = tmp;
    }

    return list;
}

static ItemHandler* root_item ()
{
    static ItemHandler *ret     = NULL;

    if (ret == NULL) {
        ret = g_object_new (ITEM_HANDLER_TYPE,
                            "type", ITEM_IS_STATIC_FOLDER,
                            "node", ExposingTree,
                            "file_path", getenv ("HOME"),
                            "exposed_name", "/", NULL);
    }

    return ret;
}

static ItemHandler* search_exposed_name_in_list (GList *items, const gchar *searchname)
{
    GList *iter;
    ItemHandler *item;
    ItemHandler *cmp;

    item = NULL;

    for (iter = items; iter; iter = g_list_next (iter)) {
        cmp = (ItemHandler*) iter->data;
        if (strcmp (item_handler_exposed_name (cmp), searchname) == 0) {
            item = cmp;
            break;
        }
    }

    return item;
}

ItemHandler* verify_exposed_path (const gchar *path)
{
    GList *path_tokens;
    GList *iter;
    GList *items;
    HierarchyNode *level;
    ItemHandler *item;

    item = nodes_cache_get_by_path (Cache, path);
    if (item != NULL)
        return item;

    if (strcmp (path, "/") == 0)
        return root_item ();

    if (ExposingTree == NULL) {
        g_warning ("Warning: there is not an exposing hierarchy");
        return NULL;
    }

    path_tokens = tokenize_path (path);

    item = NULL;
    iter = NULL;
    level = ExposingTree;

    for (iter = path_tokens; iter; iter = g_list_next (iter)) {
        items = hierarchy_node_get_subchildren (level, item);

        item = search_exposed_name_in_list (items, (const gchar*) iter->data);
        if (item == NULL)
            break;

        level = item_handler_get_logic_node (item);
    }

    easy_list_free (path_tokens);

    if (item != NULL)
        nodes_cache_set_by_path (Cache, item, g_strdup (path));

    return item;
}

void replace_hierarchy_node (ItemHandler *old_item, ItemHandler *new_item)
{
    int first;
    int second;
    gchar *buffer;
    struct stat sbuf;

    if (item_handler_stat (old_item, &sbuf) != 0)
        return;

    first = item_handler_open (old_item, O_RDONLY);
    second = item_handler_open (new_item, O_WRONLY);

    buffer = alloca (sbuf.st_size);
    read (first, buffer, sbuf.st_size);
    write (second, buffer, sbuf.st_size);

    item_handler_close (old_item, first);
    item_handler_close (new_item, second);

    /**
        TODO    Perhaps also metadata have to be moved?
    */

    item_handler_remove (old_item);
}

TrackerClient* get_tracker_client ()
{
    static TrackerClient *client        = NULL;

    if (client == NULL)
        client = tracker_connect (FALSE, G_MAXINT);

    return client;
}

NodesCache* get_cache_reference ()
{
    return Cache;
}
