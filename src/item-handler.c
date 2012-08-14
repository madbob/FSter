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

#include "item-handler.h"
#include "property-handler.h"
#include "hierarchy.h"
#include "utils.h"

#define ITEM_HANDLER_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), ITEM_HANDLER_TYPE, ItemHandlerPrivate))

#define IS_VIRTUAL(__type)                  (__type == ITEM_IS_VIRTUAL_ITEM || __type == ITEM_IS_VIRTUAL_FOLDER)

#define HAS_NOT_META(__type)                (__type == ITEM_IS_STATIC_ITEM ||   \
                                             __type == ITEM_IS_STATIC_FOLDER || \
                                             __type == ITEM_IS_MIRROR_ITEM ||   \
                                             __type == ITEM_IS_MIRROR_FOLDER)

#define IS_MIRROR(__type)                   (__type == ITEM_IS_MIRROR_ITEM || __type == ITEM_IS_MIRROR_FOLDER)

struct _ItemHandlerPrivate {
    CONTENT_TYPE    type;

    ItemHandler     *parent;
    HierarchyNode   *node;
    gchar           *exposed_name;
    gchar           *file_path;

    ContentsPlugin  *contents;

    gboolean        newly_allocated;
    gchar           *subject;
    GHashTable      *metadata;
    GHashTable      *tosave;
};

enum {
    PROP_0,
    PROP_TYPE,
    PROP_PARENT,
    PROP_NODE,
    PROP_FILE,
    PROP_EXPOSED,
    PROP_SUBJECT,
    PROP_CONTENTS,
};

G_DEFINE_TYPE (ItemHandler, item_handler, G_TYPE_OBJECT);

/*
    This is just a dummy function used to force key/value pair remove from the flushed hash table
*/
static gboolean destroy_value_in_hash (gpointer key, gpointer value, gpointer user_data)
{
    return TRUE;
}

static void flush_pending_metadata_to_save (ItemHandler *item, ...)
{
    gboolean to_free;
    gchar *stats;
    gchar *tys;
    gchar *query;
    gchar *useless;
    gchar *uri;
    va_list params;
    gpointer key;
    gpointer value;
    GList *statements;
    GList *types;
    GHashTable *table;
    GHashTableIter iter;
    GVariant *results;
    GVariant *rows;
    GVariant *sub_value;
    GVariant *sub_sub_value;
    GVariantIter r_iter;
    GVariantIter sub_iter;
    GVariantIter sub_sub_iter;
    GError *error;
    Property *prop;

    statements = NULL;
    types = NULL;
    va_start (params, item);

    while ((table = va_arg (params, GHashTable*)) != NULL) {
        to_free = va_arg (params, gboolean);
        g_hash_table_iter_init (&iter, table);

        while (g_hash_table_iter_next (&iter, &key, &value)) {
            prop = properties_pool_get_by_name ((gchar*) key);

            switch (property_get_datatype (prop)) {
                case PROPERTY_TYPE_STRING:
                    stats = g_strdup_printf ("%s \"%s\"", (gchar*) key, (gchar*) value);
                    statements = g_list_prepend (statements, stats);
                    break;

                default:
                    stats = g_strdup_printf ("%s %s", (gchar*) key, (gchar*) value);
                    statements = g_list_prepend (statements, stats);
                    break;
            }
        }

        if (to_free == TRUE)
            g_hash_table_foreach_remove (table, destroy_value_in_hash, NULL);
    }

    va_end (params);

    if (statements == NULL)
        return;

    stats = from_glist_to_string (statements, " ; ", TRUE);

    if (types == NULL) {
        query = g_strdup_printf ("INSERT { _:item a nfo:FileDataObject ; a nie:InformationElement ; %s }", stats);
    }
    else {
        tys = from_glist_to_string (types, " ; ", TRUE);
        query = g_strdup_printf ("INSERT { _:item a nfo:FileDataObject ; a nie:InformationElement ; %s ; %s }", tys, stats);
        g_free (tys);
    }

    g_free (stats);

    error = NULL;
    results = execute_update_blank (query, &error);

    if (error != NULL) {
        g_warning ("Error while saving metadata: %s", error->message);
        g_error_free (error);
    }
    else {
        /*
            To know how to iter a SparqlUpdateBlank response, cfr.
            http://mail.gnome.org/archives/commits-list/2011-February/msg05384.html
        */
        g_variant_iter_init (&r_iter, results);

        if ((rows = g_variant_iter_next_value (&r_iter))) {
            g_variant_iter_init (&sub_iter, rows);

            if ((sub_value = g_variant_iter_next_value (&sub_iter))) {
                g_variant_iter_init (&sub_sub_iter, sub_value);

                if ((sub_sub_value = g_variant_iter_next_value (&sub_sub_iter))) {
                    useless = NULL;
                    uri = NULL;
                    g_variant_get (sub_sub_value, "{ss}", &useless, &uri);
                    item->priv->subject = g_strdup (uri);
                    g_variant_unref (sub_sub_value);
                }

                g_variant_unref (sub_value);
            }

            g_variant_unref (rows);
        }
    }

    g_free (query);
}

static void item_handler_finalize (GObject *item)
{
    ItemHandler *ret;

    ret = ITEM_HANDLER (item);

    item_handler_flush (ret);
    g_hash_table_destroy (ret->priv->metadata);
    g_hash_table_destroy (ret->priv->tosave);

    if (ret->priv->exposed_name != NULL)
        g_free (ret->priv->exposed_name);

    if (ret->priv->file_path != NULL)
        g_free (ret->priv->file_path);
}

static gchar* escape_exposed_name (const gchar *str)
{
    register int i;
    register int e;
    int len;
    gchar *final;

    len = strlen (str);
    final = alloca (len);

    for (i = 0, e = 0; i < len; i++, e++) {
        if (str [i] == '/') {
            final [e] = '\\';
        }
        else {
            final [e] = str [i];
        }
    }

    final [e] = '\0';
    return g_strdup (final);
}

static void item_handler_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    ItemHandler *self = ITEM_HANDLER (object);

    switch (property_id) {
        case PROP_TYPE:
            self->priv->type = g_value_get_int (value);
            break;

        case PROP_PARENT:
            self->priv->parent = g_value_dup_object (value);
            break;

        case PROP_NODE:
            self->priv->node = g_value_dup_object (value);
            break;

        case PROP_FILE:
            if (self->priv->file_path != NULL)
                g_free (self->priv->file_path);
            self->priv->file_path = g_value_dup_string (value);
            break;

        case PROP_EXPOSED:
            if (self->priv->exposed_name != NULL)
                g_free (self->priv->exposed_name);
            self->priv->exposed_name = escape_exposed_name (g_value_get_string (value));
            break;

        case PROP_SUBJECT:
            if (self->priv->subject != NULL)
                g_free (self->priv->subject);
            self->priv->subject = g_value_dup_string (value);
            break;

        case PROP_CONTENTS:
            self->priv->contents = g_value_get_object (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void item_handler_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    ItemHandler *self = ITEM_HANDLER (object);

    switch (property_id) {
        case PROP_TYPE:
            g_value_set_int (value, self->priv->type);
            break;

        case PROP_PARENT:
            g_value_set_object (value, self->priv->parent);
            break;

        case PROP_NODE:
            g_value_set_object (value, self->priv->node);
            break;

        case PROP_FILE:
            g_value_set_string (value, self->priv->file_path);
            break;

        case PROP_EXPOSED:
            g_value_set_string (value, self->priv->exposed_name);
            break;

        case PROP_SUBJECT:
            g_value_set_string (value, self->priv->subject);
            break;

        case PROP_CONTENTS:
            g_value_set_object (value, self->priv->contents);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void item_handler_class_init (ItemHandlerClass *klass)
{
    GObjectClass *gobject_class;
    GParamSpec *param_spec;

    g_type_class_add_private (klass, sizeof (ItemHandlerPrivate));

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->finalize = item_handler_finalize;
    gobject_class->set_property = item_handler_set_property;
    gobject_class->get_property = item_handler_get_property;

    param_spec = g_param_spec_int ("type",
                                        "Item's type",
                                        "Type of the item",
                                        ITEM_IS_VIRTUAL_ITEM,
                                        ITEM_IS_SET_FOLDER,
                                        ITEM_IS_VIRTUAL_ITEM,
                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_TYPE, param_spec);

    param_spec = g_param_spec_object ("parent",
                                        "Parent ItemHandler",
                                        "ItemHandler having this as a child",
                                        ITEM_HANDLER_TYPE,
                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_PARENT, param_spec);

    param_spec = g_param_spec_object ("node",
                                        "Hierarchy node",
                                        "Hierarchy node in which this item is contained",
                                        HIERARCHY_NODE_TYPE,
                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_NODE, param_spec);

    param_spec = g_param_spec_string ("file_path",
                                        "Filesystem path",
                                        "Path of the effective file for the item",
                                        NULL,
                                        G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_FILE, param_spec);

    param_spec = g_param_spec_string ("exposed_name",
                                        "Exposed name",
                                        "Name for the item on the filesystem",
                                        NULL,
                                        G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_EXPOSED, param_spec);

    param_spec = g_param_spec_string ("subject",
                                        "Tracker subject",
                                        "Identifier of the item in the metadata storage",
                                        NULL,
                                        G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_SUBJECT, param_spec);

    param_spec = g_param_spec_object ("contents_handler",
                                        "Contents Handler",
                                        "Manager of the contents for the item",
                                        CONTENTS_PLUGIN_TYPE,
                                        G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_CONTENTS, param_spec);
}

static void item_handler_init (ItemHandler *item)
{
    item->priv = ITEM_HANDLER_GET_PRIVATE (item);
    memset (item->priv, 0, sizeof (ItemHandlerPrivate));

    item->priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    item->priv->tosave = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

/**
 * item_handler_new_alloc:
 * @type: type of the new #ItemHandler
 * @node: logical node describing the item in the hierarchy
 * @parent: direct item of the upper level in the hierarchy
 *
 * Allocates a new item. Please note this is not placed in the hierarchy, and
 * it is not saved on Tracker: on the beginning all essential metadata are
 * assigned out from here
 *
 * Return value: a newly allocated #ItemHandler
 **/
ItemHandler* item_handler_new_alloc (CONTENT_TYPE type, HierarchyNode *node, ItemHandler *parent)
{
    ItemHandler *ret;

    ret = g_object_new (ITEM_HANDLER_TYPE, "type", type, "node", node, "parent", parent, NULL);
    ret->priv->newly_allocated = TRUE;
    return ret;
}

/**
 * item_handler_get_format:
 * @item: an #ItemHandler
 *
 * To retrieve the type of the @item. From this value depends some behaviour
 *
 * Return value: type of the specified #ItemHandler
 **/
CONTENT_TYPE item_handler_get_format (ItemHandler *item)
{
    g_assert (item != NULL);
    return item->priv->type;
}

/**
 * item_handler_get_parent:
 * @item: an #ItemHandler
 *
 * Get the @item 's direct parent in the hierarchy
 *
 * Return value: the main #ItemHandler
 **/
ItemHandler* item_handler_get_parent (ItemHandler *item)
{
    g_assert (item != NULL);
    return item->priv->parent;
}

/**
 * item_handler_get_logic_node:
 * @item: an #ItemHandler
 *
 * Get the logical hierarchy node from which @item depends
 *
 * Return value: the main #HierarchyNode describing the #ItemHandler
 **/
HierarchyNode* item_handler_get_logic_node (ItemHandler *item)
{
    g_assert (item != NULL);
    return item->priv->node;
}

/**
 * item_handler_get_children:
 * @item: an #ItemHandler
 *
 * To retrieve list of all #ItemHandler which belong to @item
 *
 * Return value: a list of #ItemHandler, to be freed with g_list_free() when
 * no longer in use
 **/
GList* item_handler_get_children (ItemHandler *item)
{
    g_assert (item != NULL);

    if (item_handler_is_folder (item) == FALSE) {
        g_warning ("Required children for leaf item");
        return NULL;
    }

    return hierarchy_node_get_subchildren (item_handler_get_logic_node (item), item);
}

/**
 * item_handler_get_hidden:
 * @item: an #ItemHandler
 *
 * To check if the given @item has to be hidden while listing the parent
 * folder or not.
 * The current hiding policy is to mask only elements in the root of a node
 * signed with "hidden=yes" in the configuration (actually, only
 * system_folders), but not those which appear deeper in the hierarchy. This
 * is to permit chrooted applications to list system and configuration
 * folders (e.g. OpenOffice checks for translation files)
 *
 * Return value: %TRUE if the #ItemHandler has to be hidden, %FALSE otherwise
 **/
gboolean item_handler_get_hidden (ItemHandler *item)
{
    ItemHandler *parent;
    HierarchyNode *node;

    node = item_handler_get_logic_node (item);
    parent = item_handler_get_parent (item);
    return (hierarchy_node_hide_contents (node)) && (parent == NULL || node != item_handler_get_logic_node (parent));
}

/**
 * item_handler_exposed_name:
 * @item: an #ItemHandler
 *
 * Get the exposed name for the #ItemHandler, to be presented on the
 * filesystem
 *
 * Return value: the public name for the @item. The value is owned by the
 * object and should not be modified or freed.
 **/
const gchar* item_handler_exposed_name (ItemHandler *item)
{
    int format;
    gchar *name;

    g_assert (item != NULL);

    if (item->priv->exposed_name == NULL) {
        format = item_handler_get_format (item);

        if (format != ITEM_IS_VIRTUAL_ITEM && format != ITEM_IS_VIRTUAL_FOLDER) {
            g_warning ("Non-virtual item has no exposed name");
        }
        else {
            name = hierarchy_node_exposed_name_for_item (item_handler_get_logic_node (item), item);
            g_object_set (item, "exposed_name", name, NULL);
            g_free (name);
        }
    }

    return (const gchar*) item->priv->exposed_name;
}

static const gchar* fetch_metadata (ItemHandler *item, const gchar *metadata)
{
    gchar *query;
    gchar *ret;
    gchar *str;
    GVariant *response;
    GVariantIter *iter;
    GVariantIter *subiter;
    GError *error;

    ret = NULL;
    error = NULL;
    query = g_strdup_printf ("SELECT ?a WHERE { <%s> %s ?a }", item->priv->subject, metadata);

    response = execute_query (query, &error);

    if (response == NULL) {
        g_warning ("Unable to fetch metadata: %s", error->message);
        g_error_free (error);
    }
    else {
        iter = NULL;
        subiter = NULL;

        g_variant_get (response, "(aas)", &iter);

        if (g_variant_iter_loop (iter, "as", &subiter) && (str = NULL, g_variant_iter_loop (subiter, "s", &str))) {
            ret = g_strdup (str);
            g_hash_table_insert (item->priv->metadata, g_strdup (metadata), ret);
        }

        g_variant_unref (response);
    }

    g_free (query);
    return ret;
}

/**
 * item_handler_get_subject:
 * @item: an #ItemHandler
 *
 * Retrieves the subject for @item in Tracker. May be valid only on items of
 * type ITEM_IS_VIRTUAL_ITEM
 *
 * Return value: the subject used to identify the @item
 */
const gchar* item_handler_get_subject (ItemHandler *item)
{
    return (const gchar*) item->priv->subject;
}

/**
 * item_handler_type_has_metadata:
 * @item: an #ItemHandler
 *
 * To know if the @item type has arbitrary metadata. To be used to know if
 * that can be used in building arbitrary queries or not
 *
 * Return value: TRUE if @item can be used with item_handler_get_metadata(),
 * FALSE otherwise
 **/
gboolean item_handler_type_has_metadata (ItemHandler *item)
{
    return !HAS_NOT_META (item_handler_get_format (item));
}

/**
 * item_handler_contains_metadata:
 * @item: an #ItemHandler
 * @metadata: the name of the metadata to retrieve
 *
 * Checks if a metadata is already stored in the specified #ItemHandler.
 * Please note this do not means "the item has this metadata assigned", but
 * only checks the local cache of metadata without ask Tracker. To be used to
 * optimize queries using values already fetched
 *
 * Return value: TRUE if @item already has @metadata in the local structure,
 * FALSE otherwise
 **/
gboolean item_handler_contains_metadata (ItemHandler *item, const gchar *metadata)
{
    return g_hash_table_lookup_extended (item->priv->metadata, metadata, NULL, NULL);
}

/**
 * item_handler_get_metadata:
 * @item: an #ItemHandler
 * @metadata: the name of the metadata to retrieve
 *
 * Retrieves @metadata in @item
 *
 * Return value: value for @metadata, or NULL if no metadata with the
 * provided name is found in @item. The value is owned by the object and
 * should not be modified or freed.
 **/
const gchar* item_handler_get_metadata (ItemHandler *item, const gchar *metadata)
{
    const gchar *ret;

    g_assert (item != NULL);
    g_assert (metadata != NULL);

    if (HAS_NOT_META (item_handler_get_format (item))) {
        g_warning ("Attempt to access metadata in non-semantic hierarchy node");
        return NULL;
    }

    ret = NULL;

    if (g_hash_table_lookup_extended (item->priv->metadata, metadata, NULL, (gpointer*) &ret) == FALSE)
        if (g_hash_table_lookup_extended (item->priv->tosave, metadata, NULL, (gpointer*) &ret) == FALSE)
            ret = fetch_metadata (item, metadata);

    return ret;
}

/**
 * item_handler_get_all_metadata:
 * @item: an #ItemHandler
 *
 * Permits to obtain the list of all metadata attached to an #ItemHandler.
 * Those names can be then used with item_handler_get_metadata()
 *
 * Return value: a list of Property, each of them rappresenting one of the metadata
 * assigned to @item
 **/
GList* item_handler_get_all_metadata (ItemHandler *item)
{
    gchar *query;
    gchar *predicate;
    gchar *value;
    GList *ret;
    GVariant *response;
    GVariantIter *iter;
    GVariantIter *subiter;
    GError *error;
    Property *prop;

    ret = NULL;
    error = NULL;
    query = g_strdup_printf ("SELECT ?predicate ?value WHERE { <%s> ?predicate ?value }", item_handler_get_subject (item));
    response = execute_query (query, &error);

    if (response == NULL) {
        g_warning ("Unable to fetch all metadata: %s", error->message);
        g_error_free (error);
    }
    else {
        iter = NULL;
        subiter = NULL;

        g_variant_get (response, "(aas)", &iter);

        while (g_variant_iter_loop (iter, "as", &subiter)) {
            predicate = NULL;
            value = NULL;

            if (g_variant_iter_loop (subiter, "s", &predicate) && g_variant_iter_loop (subiter, "s", &value)) {
                prop = properties_pool_get_by_uri (predicate);
                item_handler_load_metadata (item, property_get_name (prop), value);
                ret = g_list_prepend (ret, prop);
            }
        }

        g_object_unref (response);
    }

    g_free (query);
    return g_list_reverse (ret);
}

/**
 * item_handler_set_metadata:
 * @item: an #ItemHandler
 * @metadata: name of the metadata to set
 * @value: value assigned for @metadata
 *
 * Sets a metadata named @metadata in @item with the provided @value. If
 * @metadata was already assigned it is overwritten.
 * No checks are performed to validate the metadata name
 **/
void item_handler_set_metadata (ItemHandler *item, const char *metadata, const gchar *value)
{
    if (HAS_NOT_META (item_handler_get_format (item))) {
        g_warning ("Attempt to access metadata in non-semantic hierarchy node");
        return;
    }

    g_hash_table_insert (item->priv->metadata, g_strdup (metadata), g_strdup (value));
    g_hash_table_insert (item->priv->tosave, g_strdup (metadata), g_strdup (value));
}

/**
 * item_handler_load_metadata:
 * @item:
 * @metadata:
 * @value:
 *
 * Sets a metadata named @metadata in @item with provided @value. Different
 * from item_handler_set_metadata() since this is used only to populate the
 * local data structure and has no correlation with permanent storage
 */
void item_handler_load_metadata (ItemHandler *item, const gchar *metadata, const gchar *value)
{
    if (HAS_NOT_META (item_handler_get_format (item))) {
        g_warning ("Attempt to access metadata in non-semantic hierarchy node");
        return;
    }

    g_hash_table_insert (item->priv->metadata, g_strdup (metadata), g_strdup (value));
}

static const gchar* get_file_path (ItemHandler *item)
{
    const gchar *path;
    gchar *file_path;
    CONTENT_TYPE type;

    if (item->priv->file_path == NULL) {
        type = item_handler_get_format (item);

        if (item->priv->contents != NULL) {
            file_path = contents_plugin_get_file (item->priv->contents, item);
            g_object_set (item, "file_path", file_path, NULL);
            g_free (file_path);
        }
        else {
            if (IS_VIRTUAL (type)) {
                path = item_handler_get_metadata (item, "nie:url");

                if (path != NULL) {
                    file_path = g_filename_from_uri (path, NULL, NULL);
                    g_object_set (item, "file_path", file_path, NULL);
                    g_free (file_path);
                }
            }
            else if (HAS_NOT_META (type)) {
                /*
                    Only way to know the real path of a mirror item is to set it on
                    creation time. If it is not set, no way to guess it
                */
                g_warning ("Undefined path for mirror filesystem hierarchy node");
            }
        }
    }

    return (const gchar*) item->priv->file_path;
}

static const gchar* get_some_file_path (ItemHandler *item)
{
    const gchar *ret;

    ret = get_file_path (item);
    if (ret != NULL)
        return ret;

    if (item_handler_is_folder (item))
        return DUMMY_DIRPATH;
    else
        return DUMMY_FILEPATH;
}

/**
 * item_handler_open:
 * @item: an #ItemHandler
 * @flags: flags used when opening the #ItemHandler file, the same usually
 * used with open(2)
 *
 * This is similar to
 * open(item_handler_get_metadata(@item, REAL_PATH_METADATA), @flags),
 * but internally provides a local counter for openings so to be aware of
 * modifications and invoke Tracker appropriately for re-indexing
 *
 * Return value: a file descriptor, to use with common POSIX functions such
 * as read(2) and write(2). When work is finished, pass it aside the
 * #ItemHandler itself to item_handler_close() to finalize the session
 **/
int item_handler_open (ItemHandler *item, int flags)
{
    int ret;
    const gchar *path;

    path = get_file_path (item);

    if (path != NULL) {
        ret = open (path, flags);
        if (ret == -1)
            ret = -errno;
    }
    else {
        ret = -ENOENT;
    }

    g_object_ref (item);
    return ret;
}

/**
 * item_handler_close:
 * @item: an #ItemHandler
 * @fd: the file descriptor opened over @item with item_handler_open()
 *
 * Closes an #ItemHandler opened with item_handler_open(), and decreases its
 * opening counter
 **/
void item_handler_close (ItemHandler *item, int fd)
{
    if (fd >= 0)
        close (fd);

    g_object_unref (item);
}

/**
 * item_handler_remove:
 * @item: an #ItemHandler
 *
 * Destroyes the @item, removing all related metadata from Tracker and
 * deleting the real file it wraps.
 * Attention: this function do not update the running nodes cache, please
 * provide elsewhere
 */
void item_handler_remove (ItemHandler *item)
{
    const gchar *id;
    gchar *query;
    GError *error;

    if (IS_VIRTUAL (item_handler_get_format (item))) {
        error = NULL;
        id = item_handler_get_subject (item);
        query = g_strdup_printf ("DELETE { <%s> ?predicate ?value } WHERE { <%s> ?predicate ?value }", id, id);
        execute_update (query, &error);
        g_free (query);

        if (error != NULL) {
            g_warning ("Error while removing item: %s", error->message);
            g_error_free (error);
            return;
        }
    }

    id = get_file_path (item);
    if (id != NULL)
        remove (id);
}

/**
 * item_handler_stat:
 * @item: an #ItemHandler
 * @sbuf: an empty stat struct to be filled
 *
 * To retrieve essential informations about the real file wrapped by the
 * #ItemHandler
 *
 * Return value: 0 if the struct is filled correctly, or a negative value
 * holding the relative errno
 **/
int item_handler_stat (ItemHandler *item, struct stat *sbuf)
{
    int res;
    const gchar *path;

    if (item == NULL)
        return -ENOENT;

    path = get_some_file_path (item);
    res = lstat (path, sbuf);

    /*
        This is to force items listed under a <folder> node to appear as browseable folders.
        For "mirror" items, we just get the original stat() result
    */
    if (IS_MIRROR (item_handler_get_format (item)) == FALSE && item_handler_is_folder (item)) {
        sbuf->st_mode &= !(S_IFREG);
        sbuf->st_mode |= S_IFDIR;
    }

    if (res == -1)
        return -errno;
    else
        return 0;
}

/**
 * item_handler_access:
 * @item: an #ItemHandler
 * @mask: permission to test
 *
 * To verify accessibility of a file wrapped by the specified #ItemHandler
 *
 * Return value: 0 if the required permissions mask matches with effective
 * permissions, or a negative value holding the relative errno
 **/
int item_handler_access (ItemHandler *item, int mask)
{
    int res;
    const gchar *path;

    if (item == NULL)
        return -ENOENT;

    path = get_some_file_path (item);
    res = access (path, mask);

    if (res == -1)
        return -errno;
    else
        return 0;
}

/**
 * item_handler_chmod:
 * @item: an #ItemHandler
 * @mode: new permissions mask for the item
 *
 * To change permissions of a file wrapped by the specified #ItemHandler
 *
 * Return value: 0 if the required permissions mask matches with effective
 * permissions, or a negative value holding the relative errno
 **/
int item_handler_chmod (ItemHandler *item, mode_t mode)
{
    int res;
    const gchar *path;

    if (item == NULL)
        return -ENOENT;

    path = get_some_file_path (item);
    res = chmod (path, mode);

    if (res == -1)
        return -errno;
    else
        return 0;
}

/**
 * item_handler_chown:
 * @item: an #ItemHandler
 * @uid: new user owner of the file
 * @gid: new group owner of the file
 *
 * To change user and gorup owners of a file wrapped by the specified
 * #ItemHandler
 *
 * Return value: 0 if the required permissions mask matches with effective
 * permissions, or a negative value holding the relative errno
 **/
int item_handler_chown (ItemHandler *item, uid_t uid, gid_t gid)
{
    int res;
    const gchar *path;

    if (item == NULL)
        return -ENOENT;

    path = get_some_file_path (item);
    res = chown (path, uid, gid);

    if (res == -1)
        return -errno;
    else
        return 0;
}

/**
 * item_handler_readlink:
 * @item: an #ItemHandler
 * @buf: buffer to be filled with link path
 * @size: allocation size for @buf
 *
 * To access a link hold by an #ItemHandler. To be used for ITEM_IS_MIRROR_ITEM items
 *
 * Return value: 0 if the required @item is a valid link and the linked path is copied in @buf,
 * or a negative value holding the relative errno
 **/
int item_handler_readlink (ItemHandler *item, char *buf, size_t size)
{
    int res;
    const gchar *path;

    if (item == NULL)
        return -ENOENT;

    path = get_some_file_path (item);
    memset (buf, 0, size);
    res = readlink (path, buf, size);

    if (res == -1)
        return -errno;
    else
        return 0;
}

/**
 * item_handler_truncate:
 * @item: an #ItemHandler
 * @size: size to which truncate the file
 *
 * To truncate the file managed by @item to the specified @size
 *
 * Return value: 0 if the required file is successfully truncated, or a negative value holding
 * the relative errno
 **/
int item_handler_truncate (ItemHandler *item, off_t size)
{
    int res;
    const gchar *path;

    if (item == NULL)
        return -ENOENT;

    path = get_some_file_path (item);
    res = truncate (path, size);

    if (res == -1)
        return -errno;
    else
        return 0;
}

/**
 * item_handler_utimes:
 * @item: an #ItemHandler
 * @tv: structure with new times to assign to the file: in tv[0] must be the new access time, in
 * tv[1] the new modification time
 *
 * Used to modify access and modification time for the file managed by @item
 *
 * Return value: 0 if the required file is successfully modified, or a negative value holding
 * the relative errno
 **/
int item_handler_utimes (ItemHandler *item, struct timeval tv [2])
{
    int res;
    const gchar *path;

    if (item == NULL)
        return -ENOENT;

    path = get_some_file_path (item);
    res = utimes (path, tv);

    if (res == -1)
        return -errno;
    else
        return 0;
}

/**
 * item_handler_is_folder:
 * @item: an #ItemHandler
 *
 * To ask if a #ItemHandler holds a simple file or a folder
 *
 * Return value: TRUE if the @item is a folder, FALSE otherwise
 **/
gboolean item_handler_is_folder (ItemHandler *item)
{
    CONTENT_TYPE type;

    type = item_handler_get_format (item);
    return (type == ITEM_IS_VIRTUAL_FOLDER || type == ITEM_IS_MIRROR_FOLDER ||
            type == ITEM_IS_STATIC_FOLDER || type == ITEM_IS_SET_FOLDER);
}

/**
 * item_handler_real_path:
 * @item: an #ItemHandler
 *
 * Retrieves the path of the real file wrapped by @item. To be used carefully, please use the
 * appropriate functions to access the real contents of the element
 *
 * Return value: the real path of the #ItemHandler, or NULL if none is managed
 **/
const gchar* item_handler_real_path (ItemHandler *item)
{
    return get_file_path (item);
}

/**
 * item_handler_attach_child:
 * @item: an #ItemHandler
 * @type: type of the new item to attach as @item 's child
 * @newname: name for the new item
 *
 * Creates a new item in the hierarchy, as child of @item. The new node will be created accordly
 * to the "saving policy" of the parent HierarchyNode for @item
 *
 * Return value: reference to the newly created item, or NULL if an error occours
 **/
ItemHandler* item_handler_attach_child (ItemHandler *item, NODE_TYPE type, const gchar *newname)
{
    if (item_handler_is_folder (item) == FALSE) {
        g_warning ("Trying to attach child to leaf item");
        return NULL;
    }
    else {
        return hierarchy_node_add_item (item_handler_get_logic_node (item), type, item, newname);
    }
}

/**
 * item_handler_flush:
 * @item: an #ItemHandler
 *
 * Saves permanently all metadata assigned to @item and not yet in Tracker. This is automatically
 * called on #ItemHandler destruction
 **/
void item_handler_flush (ItemHandler *item)
{
    if (item->priv->newly_allocated == TRUE)
        flush_pending_metadata_to_save (item, item->priv->tosave, TRUE, item->priv->metadata, FALSE, NULL);
    else
        flush_pending_metadata_to_save (item, item->priv->tosave, TRUE, NULL);
}
