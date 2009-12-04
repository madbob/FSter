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

#include "nodes-cache.h"

#define NODES_CACHE_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), NODES_CACHE_TYPE, NodesCachePrivate))

struct _NodesCachePrivate {
    GHashTable      *bag;
};

G_DEFINE_TYPE (NodesCache, nodes_cache, G_TYPE_OBJECT);

static void nodes_cache_finalize (GObject *cache)
{
    NodesCache *ret;

    ret = NODES_CACHE (cache);
    g_hash_table_destroy (ret->priv->bag);
}

static void nodes_cache_class_init (NodesCacheClass *klass)
{
    GObjectClass *gobject_class;

    g_type_class_add_private (klass, sizeof (NodesCachePrivate));

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->finalize = nodes_cache_finalize;
}

static void nodes_cache_init (NodesCache *cache)
{
    cache->priv = NODES_CACHE_GET_PRIVATE (cache);
    memset (cache->priv, 0, sizeof (NodesCachePrivate));
    cache->priv->bag = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/**
 * nodes_cache_new:
 *
 * Allocates a new cache where to store references to already allocated and
 * filled #ItemHandler. The purpose is to avoid fetching data from Tracker
 * and/or iterate on the real filesystem if data has been already took
 * previously.
 * Best way to use the cache is to associate to each #ItemHandler the
 * absolute path where them are found on the virtual filesystem
 *
 * Return value: a new #NodesCache
 **/
NodesCache* nodes_cache_new ()
{
    NodesCache *ret;

    ret = g_object_new (NODES_CACHE_TYPE, NULL);
    ret->priv->bag = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
    return ret;
}

/**
 * nodes_cache_get_by_path:
 * @cache: instance of #NodesCache to query
 * @path: path to look for
 *
 * Given an absolute path relative to the filesystem, looks for the related
 * #ItemHandler
 *
 * Return value: the #ItemHandler found at @path, or NULL if nothing has been
 * cached yet
 **/
ItemHandler* nodes_cache_get_by_path (NodesCache *cache, const gchar *path)
{
    return (ItemHandler*) g_hash_table_lookup (cache->priv->bag, path);
}

/**
 * nodes_cache_set_by_path:
 * @cache: instance of #NodesCache to populate
 * @item: new #ItemHandler to save in the cache
 * @path: absolute path where to retrieve @item
 *
 * Adds a new item in the cache, so to be retrieved with
 * nodes_cache_get_by_path(). Please note this function do not overwrite
 * existing elements already in cache: if @path is already in, the function
 * do nothing.
 **/
void nodes_cache_set_by_path (NodesCache *cache, ItemHandler *item, const gchar *path)
{
    if (nodes_cache_get_by_path (cache, path) == NULL)
        g_hash_table_insert (cache->priv->bag, (gchar*) path, item);
}
