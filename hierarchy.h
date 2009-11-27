/*  Copyright (C) 2009 Itsme S.r.L.
 *
 *  This file is part of Guglielmo
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

#ifndef __HIERARCHY_DEFINITIONS_H__
#define __HIERARCHY_DEFINITIONS_H__

#include "common.h"

typedef gboolean (HierarchyForeachCallback) (WrappedItem *item, gpointer userdata);

typedef enum {
    NODE_INVALID,
    NODE_ITEM,
    NODE_FILE,
    NODE_FOLDER
} NODE_TYPE;

void                build_hierarchy_tree_from_xml           (xmlDocPtr doc);
void                destroy_hierarchy_tree                  ();

HIERARCHY_LEVEL     verify_exposed_path                     (const gchar *path, WrappedItem **target, gchar *name);
const gchar*        get_exposed_name                        (WrappedItem *node);
const gchar*        get_real_path                           (WrappedItem *node);

void                hierarchy_foreach_content               (WrappedItem *root, HierarchyForeachCallback callback, gpointer userdata);
WrappedItem*        create_hierarchy_node                   (WrappedItem *parent, const gchar *path, NODE_TYPE type);
gboolean            modify_hierarchy_node                   (WrappedItem *node, WrappedItem *parent, const gchar *destination);
void                remove_hierarchy_node                   (WrappedItem *node);
int                 hierarchy_node_open                     (WrappedItem *node, int flags);
void                hierarchy_node_close                    (WrappedItem *node, int fd, gboolean written);

#endif
