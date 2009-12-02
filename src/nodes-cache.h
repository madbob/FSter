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

#ifndef NODES_CACHE_H
#define NODES_CACHE_H

#include "core.h"
#include "item-handler.h"

#define NODES_CACHE_TYPE                (nodes_cache_get_type ())
#define NODES_CACHE(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj),     \
                                         NODES_CACHE_TYPE, NodesCache))
#define NODES_CACHE_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass),      \
                                         NODES_CACHE_TYPE,                      \
                                         NodesCacheClass))
#define IS_NODES_CACHE(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj),     \
                                         NODES_CACHE_TYPE))
#define IS_NODES_CACHE_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass),      \
                                         NODES_CACHE_TYPE))
#define NODES_CACHE_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj),      \
                                         NODES_CACHE_TYPE,                      \
                                         NodesCacheClass))

typedef struct _NodesCache         NodesCache;
typedef struct _NodesCacheClass    NodesCacheClass;
typedef struct _NodesCachePrivate  NodesCachePrivate;

struct _NodesCache {
    GObject                 parent;
    NodesCachePrivate       *priv;
};

struct _NodesCacheClass {
    GObjectClass    parent_class;
};

GType           nodes_cache_get_type            ();

NodesCache*     nodes_cache_new                 ();

ItemHandler*    nodes_cache_get_by_path         (NodesCache *cache, const gchar *path);
void            nodes_cache_set_by_path         (NodesCache *cache, ItemHandler *item, const gchar *path);

#endif
