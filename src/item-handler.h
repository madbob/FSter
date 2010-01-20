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

#ifndef ITEM_HANDLER_H
#define ITEM_HANDLER_H

#include "common.h"

#define ITEM_HANDLER_TYPE             (item_handler_get_type ())
#define ITEM_HANDLER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj),   \
                                       ITEM_HANDLER_TYPE, ItemHandler))
#define ITEM_HANDLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),    \
                                       ITEM_HANDLER_TYPE,                   \
                                       ItemHandlerClass))
#define IS_ITEM_HANDLER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj),   \
                                       ITEM_HANDLER_TYPE))
#define IS_ITEM_HANDLER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),    \
                                       ITEM_HANDLER_TYPE))
#define ITEM_HANDLER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),    \
                                       ITEM_HANDLER_TYPE,                   \
                                       ItemHandlerClass))

typedef struct _ItemHandler         ItemHandler;
typedef struct _ItemHandlerClass    ItemHandlerClass;
typedef struct _ItemHandlerPrivate  ItemHandlerPrivate;

struct _ItemHandler {
    GObject             parent;
    ItemHandlerPrivate  *priv;
};

struct _ItemHandlerClass {
    GObjectClass    parent_class;
};

typedef enum {
    ITEM_IS_ROOT,
    ITEM_IS_VIRTUAL_ITEM,
    ITEM_IS_VIRTUAL_FOLDER,
    ITEM_IS_MIRROR_ITEM,
    ITEM_IS_MIRROR_FOLDER,
    ITEM_IS_STATIC_ITEM,
    ITEM_IS_STATIC_FOLDER,
    ITEM_IS_SET_FOLDER,
} CONTENT_TYPE;

/*
    This is used to specify if a new item has to be a folder or a file. The effective type of the
    item (CONTENT_TYPE) is automatically assigned in function of the parent HierarchyNode
*/
typedef enum {
    NODE_IS_FOLDER,
    NODE_IS_FILE
} NODE_TYPE;

#include "hierarchy-node.h"

GType           item_handler_get_type           ();

ItemHandler*    item_handler_new_alloc          (CONTENT_TYPE type, HierarchyNode *node, ItemHandler *parent);

CONTENT_TYPE    item_handler_get_format         (ItemHandler *item);
ItemHandler*    item_handler_get_parent         (ItemHandler *item);
HierarchyNode*  item_handler_get_logic_node     (ItemHandler *item);
GList*          item_handler_get_children       (ItemHandler *item);

const gchar*    item_handler_exposed_name       (ItemHandler *item);
int             item_handler_open               (ItemHandler *item, int flags);
void            item_handler_close              (ItemHandler *item, int fd);
int             item_handler_stat               (ItemHandler *item, struct stat *sbuf);
int             item_handler_access             (ItemHandler *item, int mask);
int             item_handler_chmod              (ItemHandler *item, mode_t mode);
int             item_handler_chown              (ItemHandler *item, uid_t uid, gid_t gid);
int             item_handler_readlink           (ItemHandler *item, char *buf, size_t size);
int             item_handler_truncate           (ItemHandler *item, off_t size);
int             item_handler_utimes             (ItemHandler *item, struct timeval tv [2]);
gboolean        item_handler_is_folder          (ItemHandler *item);
const gchar*    item_handler_real_path          (ItemHandler *item);

ItemHandler*    item_handler_attach_child       (ItemHandler *item, NODE_TYPE type, const gchar *newname);
void            item_handler_remove             (ItemHandler *item);

const gchar*    item_handler_get_subject        (ItemHandler *item);
gboolean        item_handler_type_has_metadata  (ItemHandler *item);
gboolean        item_handler_contains_metadata  (ItemHandler *item, const gchar *name);
const gchar*    item_handler_get_metadata       (ItemHandler *item, const gchar *name);
GList*          item_handler_get_all_metadata   (ItemHandler *item);
void            item_handler_set_metadata       (ItemHandler *item, const gchar *name, const gchar *value);
void            item_handler_load_metadata      (ItemHandler *item, const gchar *name, const gchar *value);
void            item_handler_flush              (ItemHandler *item);

#endif
