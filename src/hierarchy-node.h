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

#ifndef HIERARCHY_NODE_H
#define HIERARCHY_NODE_H

#include "common.h"

#define HIERARCHY_NODE_TYPE             (hierarchy_node_get_type ())
#define HIERARCHY_NODE(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),     \
                                         HIERARCHY_NODE_TYPE, HierarchyNode))
#define HIERARCHY_NODE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),      \
                                         HIERARCHY_NODE_TYPE,                   \
                                         HierarchyNodeClass))
#define IS_HIERARCHY_NODE(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),     \
                                         HIERARCHY_NODE_TYPE))
#define IS_HIERARCHY_NODE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),      \
                                         HIERARCHY_NODE_TYPE))
#define HIERARCHY_NODE_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),      \
                                         HIERARCHY_NODE_TYPE,                   \
                                         HierarchyNodeClass))

typedef struct _HierarchyNode         HierarchyNode;
typedef struct _HierarchyNodeClass    HierarchyNodeClass;
typedef struct _HierarchyNodePrivate  HierarchyNodePrivate;

struct _HierarchyNode {
    GObject                 parent;
    HierarchyNodePrivate    *priv;
};

struct _HierarchyNodeClass {
    GObjectClass    parent_class;
};

#include "item-handler.h"

GType           hierarchy_node_get_type                     ();

HierarchyNode*  hierarchy_node_new_from_xml                 (HierarchyNode *parent, xmlNode *node);

CONTENT_TYPE    hierarchy_node_get_format                   (HierarchyNode *node);

GList*          hierarchy_node_get_children                 (HierarchyNode *node, ItemHandler *parent);
GList*          hierarchy_node_get_subchildren              (HierarchyNode *node, ItemHandler *parent);

const gchar*    hierarchy_node_get_mirror_path              (HierarchyNode *node);
gboolean        hierarchy_node_hide_contents                (HierarchyNode *node);

ItemHandler*    hierarchy_node_add_item                     (HierarchyNode *node, NODE_TYPE type, ItemHandler *parent, const gchar *name);

gchar*          hierarchy_node_exposed_name_for_item        (HierarchyNode *node, ItemHandler *item);

void            hierarchy_node_set_save_path                (gchar *path);

#endif
