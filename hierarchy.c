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

#include "common.h"
#include "hierarchy.h"

#define METADATA_DUMPS_PATH             "/tmp/.avfs_dumps/"
#define DUMMY_FILEPATH                  "/tmp/.avfs_dummy_reference"
#define DUMMY_DIRPATH                   "/tmp/.avfs_dummy_folder"
#define FAKE_SAVING_FOLDER              "/tmp/.avfs_contents"

typedef struct _HierarchyNode           HierarchyNode;
typedef struct _ExposePolicy            ExposePolicy;
typedef int (*ContentCallback)          (ExposePolicy *policy, WrappedItem *item, int flags);

typedef enum {
    METADATA_OPERATOR_IS_EQUAL,
    METADATA_OPERATOR_IS_NOT_EQUAL,
} METADATA_OPERATOR;

typedef struct {
    gchar               *metadata;
    gchar               *get_from_parent;
    gchar               *get_from_self;
    gchar               *fixed_value;
    gchar               *comparison_value;
    METADATA_OPERATOR   operator;
    int                 get_from_extraction;
    int                 condition_from_extraction;
} MetadataReference;

struct _ExposePolicy {
    gchar               *formula;
    GList               *exposed_metadata;          // list of strings
    GList               *conditional_metadata;      // list of MetadataReference
    gchar               *content_metadata;
    ContentCallback     contents_callback;
};

typedef struct {
    gboolean            inherit;
    GList               *conditions;                // list of MetadataReference
} ConditionPolicy;

typedef struct {
    gchar               *formula;
    GList               *assigned_metadata;         // list of MetadataReference
} SaveExtractName;

typedef struct {
    gboolean            inherit;
    gboolean            writable;
    GList               *inheritable_assignments;   // list of MetadataReference
    SaveExtractName     extraction_behaviour;
    gchar               *hijack_folder;
} SavePolicy;

typedef enum {
    HIERARCHY_NODE_IS_ROOT,
    HIERARCHY_NODE_IS_ITEM,
    HIERARCHY_NODE_IS_FOLDER,
    HIERARCHY_NODE_IS_STATIC_FOLDER,
    HIERARCHY_NODE_IS_SHADOW_FOLDER
} HIERARCHY_NODE_TYPE;

struct _HierarchyNode {
    HIERARCHY_NODE_TYPE type;
    gchar               *name;
    gchar               *mapped_folder;
    HierarchyNode       *parent;
    ExposePolicy        expose_policy;
    ConditionPolicy     self_policy;
    ConditionPolicy     child_policy;
    SavePolicy          save_policy;
    GList               *contents;                  // list of WrappedItem
    GList               *children;                  // list of HierarchyNode
};

typedef enum {
    REGULAR_ITEM,
    ABSTRACT_ITEM,
    FILESYSTEM_WRAP,
} WRAPPED_ITEM_TYPE;

struct _WrappedItem {
    WRAPPED_ITEM_TYPE   type;
    gchar               *exposed_name;
    gchar               *real_path;
    Item                *item;
    HierarchyNode       *hierarchy_node;
    WrappedItem         *parent;
    GList               *children;                  // list of WrappedItem
};

static HierarchyNode                    *ExposingTree;

static void free_metadata_reference (MetadataReference *ref)
{
    if (ref->metadata != NULL)
        g_free (ref->metadata);
    if (ref->get_from_parent != NULL)
        g_free (ref->get_from_parent);
    if (ref->get_from_self != NULL)
        g_free (ref->get_from_self);
    if (ref->fixed_value != NULL)
        g_free (ref->fixed_value);
    g_free (ref);
}

static void free_wrapped_item (WrappedItem *item)
{
    if (item->exposed_name != NULL) {
        g_free (item->exposed_name);
        item->exposed_name = NULL;
    }
    if (item->real_path != NULL) {
        g_free (item->real_path);
        item->real_path = NULL;
    }
    if (item->item != NULL) {
        g_object_unref (item->item);
        item->item = NULL;
    }
}

static void easy_list_free (GList *list)
{
    GList *iter;

    for (iter = list; iter; iter = g_list_next (iter))
        g_free (iter->data);

    g_list_free (list);
}

static void free_expose_policy (ExposePolicy *policy)
{
    if (policy->formula != NULL)
        g_free (policy->formula);

    if (policy->exposed_metadata != NULL)
        easy_list_free (policy->exposed_metadata);

    if (policy->content_metadata != NULL)
        g_free (policy->content_metadata);
}

static void free_condition_policy (ConditionPolicy *policy)
{
    GList *iter;

    for (iter = policy->conditions; iter; iter = g_list_next (iter))
        free_metadata_reference ((MetadataReference*) iter->data);
}

static void free_save_policy (SavePolicy *policy)
{
    GList *iter;

    for (iter = policy->inheritable_assignments; iter; iter = g_list_next (iter))
        free_metadata_reference ((MetadataReference*) iter->data);

    for (iter = policy->extraction_behaviour.assigned_metadata; iter; iter = g_list_next (iter))
        free_metadata_reference ((MetadataReference*) iter->data);

    if (policy->hijack_folder != NULL)
        g_free (policy->hijack_folder);

    if (policy->extraction_behaviour.formula != NULL)
        g_free (policy->extraction_behaviour.formula);
}

static void free_hierarchy_node (HierarchyNode *node)
{
    GList *iter;

    if (node->name != NULL)
        g_free (node->name);

    if (node->mapped_folder != NULL)
        g_free (node->mapped_folder);

    free_expose_policy (&(node->expose_policy));
    free_condition_policy (&(node->self_policy));
    free_condition_policy (&(node->child_policy));
    free_save_policy (&(node->save_policy));

    for (iter = node->contents; iter; iter = g_list_next (iter))
        free_wrapped_item ((WrappedItem*) iter->data);

    for (iter = node->children; iter; iter = g_list_next (iter))
        free_hierarchy_node ((HierarchyNode*) iter->data);
}

/**
    TODO    This function can be largely improved
*/
static void check_and_create_folder (gchar *path)
{
    gboolean ret;
    GFile *dummy;
    GError *error;

    dummy = g_file_new_for_path (path);

    if (g_file_query_exists (dummy, NULL) == FALSE) {
        error = NULL;
        ret = g_file_make_directory_with_parents (dummy, NULL, &error);

        if (ret == FALSE) {
            fprintf (stderr, "Error: unable to create directory %s\n", error->message);
            g_error_free (error);
        }
    }

    g_object_unref (dummy);
}

static gchar* expand_path_to_absolute (gchar *path)
{
    gchar *ret;

    if (path [0] == '~' && path [1] == '/')
        ret = g_build_filename (getenv ("HOME"), path + 2, NULL);
    else
        ret = g_strdup (path);

    return ret;
}

/*
    Just an utility to avoid long long strings to invoke with all macros every time.
    I'm lazy also to read my own code...
*/
static void item_set_metadata (Item *item, gchar *metadata, gchar *value, gboolean fixed)
{
    GValue *val;

    val = (fixed ? METADATA_VALUE_FROM_STATIC_STRING (value) : METADATA_VALUE_FROM_STRING (value));
    guglielmo_item_set_metadata (item, STATIC_METADATA_IDENTIFIER (metadata), val);
}

static MetadataReference* parse_reference_to_metadata (const gchar *tag, xmlNode *node)
{
    gchar *name;
    xmlNode *attr;
    MetadataReference *ref;

    if (strcmp ((gchar*) node->name, tag) != 0) {
        fprintf (stderr, "Error: expected '%s', found '%s'\n", tag, node->name);
        return NULL;
    }

    attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "metadata");
    if (attr != NULL) {
        name = (gchar*) xmlNodeGetContent (attr);
    }
    else {
        fprintf (stderr, "Error: 'metadata' tag required in definition\n");
        return NULL;
    }

    ref = g_new0 (MetadataReference, 1);
    ref->metadata = name;

    attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "operator");
    if (attr != NULL) {
        name = (gchar*) xmlNodeGetContent (attr);

        if (strcmp (name, "is") == 0)
            ref->operator = METADATA_OPERATOR_IS_EQUAL;
        else if (strcmp (name, "isnot") == 0)
            ref->operator = METADATA_OPERATOR_IS_NOT_EQUAL;

        xmlFree (name);
    }

    attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "value");
    if (attr != NULL) {
        ref->fixed_value = (gchar*) xmlNodeGetContent (attr);
    }
    else {
        attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "valuefrommetadata");
        if (attr != NULL) {
            ref->get_from_self = (gchar*) xmlNodeGetContent (attr);
        }
        else {
            attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "valuefromparentmetadata");
            if (attr != NULL) {
                ref->get_from_parent = (gchar*) xmlNodeGetContent (attr);
            }
            else {
                attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "valuefromextract");
                if (attr != NULL) {
                    name = (gchar*) xmlNodeGetContent (attr);
                    ref->get_from_extraction = strtoull (name, NULL, 10);
                    xmlFree (name);
                }
                else {
                    fprintf (stderr, "Error: unrecognized metadata assignment behaviour in %s\n", (gchar*) node->name);
                    free_metadata_reference (ref);
                    ref = NULL;
                }
            }
        }
    }

    attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "ifmetadatavalue");
    if (attr != NULL)
        ref->comparison_value = (gchar*) xmlNodeGetContent (attr);

    attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "iffromextract");
    if (attr != NULL) {
        name = (gchar*) xmlNodeGetContent (attr);
        ref->condition_from_extraction = strtoull (name, NULL, 10);
        xmlFree (name);
    }

    attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "id");
    if (attr != NULL) {
        name = (gchar*) xmlNodeGetContent (attr);
        ref->condition_from_extraction = strtoull (name, NULL, 10);
        xmlFree (name);
    }

    return ref;
}

static gint sort_extraction_metadata (gconstpointer a, gconstpointer b)
{
    MetadataReference *first;
    MetadataReference *second;

    first = (MetadataReference*) a;
    second = (MetadataReference*) b;

    /*
        The reason of this function is to order the assignment metadata list to put policies
        based on regular expressions at the beginning, sorted by extraction offset. This is to
        speed up execution of guess_metadata_from_saving_name() and avoid too many cycles into
        the list. No sorting policy is adopted for other kind of policies, in those cases
        function can retun always 0
    */

    if (first->get_from_extraction != 0) {
        if (second->get_from_extraction != 0)
            return first->get_from_extraction - second->get_from_extraction;
        else
            return -1;
    }
    else if (second->get_from_extraction != 0)
        return 1;

    return 0;
}

static gboolean parse_saving_policy (SavePolicy *saving, xmlNode *root)
{
    gchar *str;
    gboolean ret;
    xmlNode *attr;
    xmlNode *node;
    xmlNode *subnode;
    MetadataReference *ref;

    saving->inherit = TRUE;

    attr = (xmlNode*) xmlHasProp (root, (xmlChar*) "inherit");
    if (attr != NULL) {
        str = (gchar*) xmlNodeGetContent (attr);
        if (strcmp ((gchar*) str, "no") == 0)
            saving->inherit = FALSE;
        xmlFree (str);
    }

    ret = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "inheritable-metadatas") == 0) {
            for (subnode = node->children; subnode; subnode = subnode->next) {
                ref = parse_reference_to_metadata ("metadata", subnode);

                if (ref != NULL) {
                    saving->inheritable_assignments = g_list_prepend (saving->inheritable_assignments, ref);
                }
                else {
                    ret = FALSE;
                    break;
                }
            }
        }
        else if (strcmp ((gchar*) node->name, "new-file") == 0) {
            attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "extractmetadata");
            if (attr != NULL)
                saving->extraction_behaviour.formula = (gchar*) xmlNodeGetContent (attr);

            for (subnode = node->children; subnode; subnode = subnode->next) {
                ref = parse_reference_to_metadata ("metadata", subnode);

                if (ref != NULL) {
                    saving->extraction_behaviour.assigned_metadata = g_list_prepend (saving->extraction_behaviour.assigned_metadata, ref);
                }
                else {
                    ret = FALSE;
                    break;
                }
            }

            if (ret == TRUE) {
                saving->extraction_behaviour.assigned_metadata = g_list_sort (saving->extraction_behaviour.assigned_metadata, sort_extraction_metadata);
                saving->writable = TRUE;
            }
        }
        else if (strcmp ((gchar*) node->name, "new-shadow-content") == 0) {
            attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "base_path");
            if (attr != NULL) {
                str = (gchar*) xmlNodeGetContent (attr);
                saving->hijack_folder = expand_path_to_absolute (str);
                free (str);
            }

            saving->writable = TRUE;
        }
    }

    return ret;
}

static gboolean parse_exposing_formula (ExposePolicy *exposing, gchar *string)
{
    register int i;
    register int e;
    int a;
    int len;
    int offset;
    int hop;
    gchar *formula;
    gchar *end_metadata;
    gboolean ret;
    GList *metadata;
    GList *iter;

    metadata = NULL;
    offset = 1;
    len = strlen (string);
    formula = g_new0 (char, len);
    ret = TRUE;

    for (i = 0, e = 0; i < len; i++) {
        /*
            References to metadata (expressed as ${metadata} are translated in the formula as
            "@N", where N is an offset identifier used to hook the underlying list.
            Identifiers as "\N" are used to track partial strings to be set in function of a
            particular value assumed by a metadata
        */
        if (string [i] == '$' && string [i + 1] == '{') {
            a = i + 2;

            end_metadata = strchr (string + a, '}');
            if (end_metadata == NULL) {
                ret = FALSE;
                break;
            }

            *end_metadata = '\0';
            metadata = g_list_prepend (metadata, g_strdup (string + a));
            i = a + strlen (string + a);

            hop = snprintf (formula + e, len - e, "@%d", offset);
            offset++;
            e += hop;
        }
        else {
            formula [e] = string [i];
            e++;
        }
    }

    if (ret == TRUE) {
        exposing->formula = formula;
        exposing->exposed_metadata = g_list_reverse (metadata);
    }
    else {
        g_free (formula);

        for (iter = metadata; iter; iter = g_list_next (iter))
            g_free (iter->data);

        g_list_free (metadata);
    }

    return ret;
}

static int item_content_plain (ExposePolicy *policy, WrappedItem *item, int flags)
{
    const gchar *path;
    gchar *rpath;

    path = guglielmo_item_get_string_metadata (item->item, STATIC_METADATA_IDENTIFIER (policy->content_metadata));

    if (path == NULL)
        return -1;

    rpath = strdupa (path);
    NORMALIZE_REAL_PATH (rpath);
    if (access (rpath, F_OK) != 0)
        return -1;

    return open (rpath, flags);
}

static gchar* numeric_id (Item *item)
{
    const gchar *str;
    gchar *ret;
    gchar *sep;

    str = guglielmo_item_get_string_metadata (item, ID_METADATA);
    if (str == NULL)
        return NULL;

    if (strncmp (str, "<itsme://", 9) == 0) {
        ret = strdupa (str + 9);

        sep = strrchr (ret, '>');
        if (sep != NULL)
            *sep = '\0';

        return g_strdup (ret);
    }
    else {
        return g_strdup (str);
    }
}

static int item_content_dump_metadata (ExposePolicy *policy, WrappedItem *item, int flags)
{
    int fd;
    FILE *file;
    gchar *path;
    gchar *id;
    GList *metadata_list;
    GList *iter;
    GList *val_iter;
    GValue *value;

    if (strcmp (policy->content_metadata, "*") == 0) {
        id = numeric_id (item->item);
        path = g_build_filename (METADATA_DUMPS_PATH, id, NULL);
        g_free (id);

        /**
            TODO    If dump file already exists it is used, but it is not updated with latest metadata
                    assignments and changes. Provide some kind of check and update
        */
        if (access (path, F_OK) != 0) {
            metadata_list = guglielmo_item_get_all_metadata (item->item);
            file = fopen (path, "w");

            if (file == NULL) {
                fprintf (stderr, "Error dumping metadata: unable to create file in %s, %s\n", path, strerror (errno));
            }
            else {
                for (iter = metadata_list; iter; iter = g_list_next (iter)) {
                    value = guglielmo_item_get_metadata (item->item, (gchar*) iter->data);

                    if (value == NULL) {
                        fprintf (stderr, "Error dumping metadata: '%s' exists but has no value\n", (gchar*) iter->data);
                        continue;
                    }

                    fprintf (file, "%s: ", (gchar*) iter->data);

                    if (G_VALUE_HOLDS (value, G_TYPE_STRING)) {
                        fprintf (file, "%s\n", g_value_get_string (value));
                    }
                    else if (G_VALUE_HOLDS (value, G_TYPE_POINTER)) {
                        for (val_iter = g_value_get_pointer (value); val_iter; val_iter = g_list_next (val_iter))
                            fprintf (file, "%s ", (gchar*) val_iter->data);

                        fprintf (file, "\n");
                    }
                }

                g_list_free (metadata_list);
                fclose (file);
            }
        }

        fd = open (path, flags);
        g_free (path);
        return fd;
    }

    /**
        TODO    Handle different situations than "dump all metadata". A syntax to express this
                requirement is still needed in configuration specification
    */

    return -1;
}

static gint sort_conditional_metadata (gconstpointer a, gconstpointer b)
{
    MetadataReference *first;
    MetadataReference *second;

    first = (MetadataReference*) a;
    second = (MetadataReference*) b;

    if (first->get_from_extraction != 0) {
        if (second->condition_from_extraction != 0)
            return first->condition_from_extraction - second->condition_from_extraction;
        else
            return -1;
    }
    else if (second->condition_from_extraction != 0)
        return 1;

    return 0;
}


static void wire_wrapped_item (WrappedItem *item, HierarchyNode* node, WrappedItem *parent)
{
    if (node != NULL) {
        if (node->contents == NULL)
            node->contents = g_list_prepend (node->contents, item);
        else
            node->contents = g_list_append (node->contents, item);
    }

    if (parent != NULL) {
        if (parent->children == NULL)
            parent->children = g_list_prepend (parent->children, item);
        else
            parent->children = g_list_append (parent->children, item);
    }

    item->hierarchy_node = node;
    item->parent = parent;
}

static gboolean parse_exposing_policy (ExposePolicy *exposing, xmlNode *root)
{
    gchar *str;
    gboolean ret;
    xmlNode *node;
    xmlNode *subnode;
    xmlNode *attr;

    ret = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "name") == 0) {
            attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "value");
            if (attr != NULL) {
                str = (gchar*) xmlNodeGetContent (attr);

                if (parse_exposing_formula (exposing, str) == FALSE)
                    ret = FALSE;

                xmlFree (str);
            }
            else {
                attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "value");
                if (attr != NULL) {
                    str = (gchar*) xmlNodeGetContent (attr);
                    exposing->formula = str;
                }
                else {
                    fprintf (stderr, "Unable to parse exposing formula\n");
                    ret = FALSE;
                    break;
                }
            }

            if (node->children != NULL) {
                for (subnode = node->children; subnode; subnode = subnode->next)
                    exposing->conditional_metadata = g_list_prepend (exposing->conditional_metadata,
                                                                     parse_reference_to_metadata ("derivated-value", subnode));
                exposing->conditional_metadata = g_list_sort (exposing->conditional_metadata, sort_conditional_metadata);
            }
        }
        else if (strcmp ((gchar*) node->name, "content") == 0) {
            attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "type");
            if (attr != NULL) {
                str = (gchar*) xmlNodeGetContent (attr);

                /**
                    TODO    Probably contents callbacks may be better handled...
                */
                if (strcmp ((gchar*) str, "real_file") == 0) {
                    exposing->contents_callback = item_content_plain;
                }
                else if (strcmp ((gchar*) str, "dump_metadata") == 0) {
                    exposing->contents_callback = item_content_dump_metadata;
                }
                else {
                    fprintf (stderr, "Unable to identify contents exposing policy\n");
                    ret = FALSE;
                }

                xmlFree (str);
            }

            attr = (xmlNode*) xmlHasProp (node, (xmlChar*) "metadata");
            if (attr != NULL)
                exposing->content_metadata = (gchar*) xmlNodeGetContent (attr);
        }
    }

    return ret;
}

static gboolean parse_conditions_policy (ConditionPolicy *conditions, xmlNode *root)
{
    gchar *str;
    gboolean ret;
    GList *cond_list;
    GList *iter;
    xmlNode *attr;
    xmlNode *node;
    MetadataReference *cond;

    attr = (xmlNode*) xmlHasProp (root, (xmlChar*) "from-parent");
    if (attr != NULL) {
        str = (gchar*) xmlNodeGetContent (attr);

        if (strcmp (str, "no") == 0)
            conditions->inherit = FALSE;
        else
            conditions->inherit = TRUE;

        xmlFree (str);
    }

    ret = TRUE;
    cond_list = NULL;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        cond = parse_reference_to_metadata ("condition", node);
        if (cond == NULL)
            ret = FALSE;
        else
            cond_list = g_list_prepend (cond_list, cond);
    }

    if (ret == TRUE) {
        conditions->conditions = g_list_reverse (cond_list);
    }
    else {
        for (iter = cond_list; iter; iter = g_list_next (iter))
            free_metadata_reference ((MetadataReference*) iter->data);
        g_list_free (cond_list);
    }

    return ret;
}

static gint sort_hierarchy_node_child (gconstpointer a, gconstpointer b)
{
    HierarchyNode *first;
    HierarchyNode *second;

    first = (HierarchyNode*) a;
    second = (HierarchyNode*) b;

    if (first->type == HIERARCHY_NODE_IS_SHADOW_FOLDER)
        return 1;
    else
        return -1;
}

static gboolean parse_exposing_nodes (HierarchyNode *parent, xmlNode *root);

static gboolean parse_content_policy (HierarchyNode *parent, xmlNode *root)
{
    gboolean ret;
    xmlNode *node;

    ret = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "inheritable-conditions") == 0)
            ret = parse_conditions_policy (&(parent->child_policy), node);
        else
            ret = parse_exposing_nodes (parent, node);
    }

    parent->children = g_list_sort (parent->children, sort_hierarchy_node_child);
    return ret;
}

static gboolean parse_exposing_nodes (HierarchyNode *parent, xmlNode *root)
{
    gboolean ret;
    gchar *str;
    xmlNode *node;
    xmlNode *attr;
    HierarchyNode *this;
    WrappedItem *witem;

    this = g_new0 (HierarchyNode, 1);

    if (strcmp ((gchar*) root->name, "root") == 0) {
        ExposingTree = this;
        this->type = HIERARCHY_NODE_IS_ROOT;

        witem = g_new0 (WrappedItem, 1);
        witem->type = ABSTRACT_ITEM;
        witem->exposed_name = g_strdup ("/");
        wire_wrapped_item (witem, ExposingTree, NULL);
    }
    else if (strcmp ((gchar*) root->name, "file") == 0) {
        this->type = HIERARCHY_NODE_IS_ITEM;
    }
    else if (strcmp ((gchar*) root->name, "folder") == 0) {
        this->type = HIERARCHY_NODE_IS_FOLDER;
    }
    else if (strcmp ((gchar*) root->name, "static_folder") == 0) {
        this->type = HIERARCHY_NODE_IS_STATIC_FOLDER;
    }
    else if (strcmp ((gchar*) root->name, "system_folders") == 0) {
        this->type = HIERARCHY_NODE_IS_SHADOW_FOLDER;
        this->mapped_folder = g_strdup ("/");
    }
    else if (strcmp ((gchar*) root->name, "shadow-content") == 0) {
        this->type = HIERARCHY_NODE_IS_SHADOW_FOLDER;

        attr = (xmlNode*) xmlHasProp (root, (xmlChar*) "base_path");
        if (attr != NULL) {
            str = (gchar*) xmlNodeGetContent (attr);
            this->mapped_folder = expand_path_to_absolute (str);
            this->save_policy.hijack_folder = g_strdup (this->mapped_folder);
            this->save_policy.writable = TRUE;
            free (str);
        }
    }
    else {
        fprintf (stderr, "Error: unrecognized type of node, '%s'\n", root->name);
        g_free (this);
        return FALSE;
    }

    ret = TRUE;

    attr = (xmlNode*) xmlHasProp (root, (xmlChar*) "id");
    if (attr != NULL)
        this->name = (gchar*) xmlNodeGetContent (attr);

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "saving-policy") == 0)
            ret = parse_saving_policy (&(this->save_policy), node);
        else if (strcmp ((gchar*) node->name, "expose") == 0)
            ret = parse_exposing_policy (&(this->expose_policy), node);
        else if (strcmp ((gchar*) node->name, "content") == 0)
            ret = parse_content_policy (this, node);
        else if (strcmp ((gchar*) node->name, "inheritable-conditions") == 0)
            ret = parse_conditions_policy (&(this->self_policy), node);
    }

    if (ret == TRUE && parent != NULL) {
        parent->children = g_list_prepend (parent->children, this);
        this->parent = parent;
    }

    return ret;
}

static void create_dummy_references ()
{
    gchar *path;

    path = DUMMY_FILEPATH;
    fclose (fopen (path, "w"));

    mkdir (METADATA_DUMPS_PATH, 0770);
    mkdir (FAKE_SAVING_FOLDER, 0770);
    mkdir (DUMMY_DIRPATH, 0770);
}

void build_hierarchy_tree_from_xml (xmlDocPtr doc)
{
    xmlNode *root;
    xmlNode *node;

    root = xmlDocGetRootElement (doc);
    if (strcmp ((gchar*) root->name, "conf") != 0) {
        fprintf (stderr, "Error: configuration file must be enclosed in a 'conf' tag, '%s' is found\n", root->name);
        return;
    }

    for (node = root->children; node; node = node->next) {
        if (strcmp ((gchar*) node->name, "exposing-tree") == 0)
            parse_exposing_nodes (NULL, node->children);

        /**
            TODO    Handle saving-tree
        */
    }

    create_dummy_references ();
}

void destroy_hierarchy_tree ()
{
    free_hierarchy_node (ExposingTree);
}

static void calculate_exposed_name_for_node (WrappedItem *node)
{
    register int i;
    int len;
    const gchar *val;
    GList *metadata_iter;
    GList *conditional_iter;
    GString *result;
    ExposePolicy *exp;
    MetadataReference *conditional;

    if (node == NULL) {
        fprintf (stderr, "Runtime error: node to which check exposed name is NULL\n");
        return;
    }

    exp = &(node->hierarchy_node->expose_policy);

    if (exp->formula == NULL) {
        fprintf (stderr, "Runtime error: exposing formula not available in node %s\n", node->hierarchy_node->name);
        return;
    }

    len = strlen (exp->formula);
    metadata_iter = exp->exposed_metadata;
    conditional_iter = exp->conditional_metadata;
    result = g_string_new ("");

    for (i = 0; i < len; i++) {
        if (exp->formula [i] == '@' && g_ascii_isdigit (exp->formula [i + 1])) {
            if (metadata_iter == NULL) {
                /**
                    TODO    Perhaps check about configuration coherence may be executed at parsing
                */
                fprintf (stderr, "Runtime error: required a metadata not defined in configuration\n");
            }
            else {
                val = guglielmo_item_get_string_metadata (node->item, STATIC_METADATA_IDENTIFIER ((gchar*) metadata_iter->data));

                if (val != NULL)
                    g_string_append_printf (result, "%s", val);

                metadata_iter = g_list_next (metadata_iter);
            }

            i++;
        }
        else if (exp->formula [i] == '\\' && g_ascii_isdigit (exp->formula [i + 1])) {
            conditional = (MetadataReference*) conditional_iter->data;
            val = guglielmo_item_get_string_metadata (node->item, STATIC_METADATA_IDENTIFIER (conditional->metadata));

            if (val != NULL && strcmp (val, conditional->comparison_value) == 0)
                g_string_append_printf (result, "%s", conditional->fixed_value);

            conditional_iter = g_list_next (conditional_iter);
            i++;
        }
        else {
            g_string_append_printf (result, "%c", exp->formula [i]);
        }
    }

    node->exposed_name = g_string_free (result, FALSE);
}

static NODE_TYPE get_element_type (WrappedItem *item)
{
    struct stat sbuf;

    if (item->real_path == NULL)
        return NODE_INVALID;

    if (stat (item->real_path, &sbuf) != 0)
        return NODE_INVALID;

    if (S_ISDIR (sbuf.st_mode) == TRUE)
        return NODE_FOLDER;
    else
        return NODE_FILE;
}

const gchar* get_exposed_name (WrappedItem *node)
{
    if (node->exposed_name == NULL)
        calculate_exposed_name_for_node (node);

    return (const gchar*) node->exposed_name;
}

static void retrieve_item_real_path (WrappedItem *node)
{
    const gchar *val;
    gchar *rpath;

    if (node->type == REGULAR_ITEM) {
        if (node->item == NULL) {
            fprintf (stderr, "Metadata from item required, but item not set\n");
        }
        else {
            val = guglielmo_item_get_string_metadata (node->item, REAL_PATH_METADATA);

            if (val == NULL) {
                node->real_path = g_strdup (DUMMY_FILEPATH);
            }
            else {
                rpath = strdupa (val);
                NORMALIZE_REAL_PATH (rpath);
                node->real_path = g_strdup (rpath);
            }
        }
    }
    else if (node->type == ABSTRACT_ITEM) {
        node->real_path = g_strdup (DUMMY_DIRPATH);
    }
}

const gchar* get_real_path (WrappedItem *node)
{
    if (node->real_path == NULL)
        retrieve_item_real_path (node);

    return node->real_path;
}

static WrappedItem* check_named_item_in_list (GList *list, const gchar *name)
{
    const gchar *item_name;
    GList *iter;
    WrappedItem *tmp;

    for (iter = list; iter; iter = g_list_next (iter)) {
        tmp = (WrappedItem*) iter->data;
        item_name = get_exposed_name (tmp);

        if (item_name != NULL && strcmp (name, item_name) == 0)
            return tmp;
    }

    return NULL;
}

static gboolean metadata_reference_involves_parent (MetadataReference *metadata)
{
    return (metadata->get_from_parent != NULL);
}

static gchar* metadata_reference_to_query (MetadataReference *metadata, WrappedItem *ref_node)
{
    gchar *operator;
    gchar *ret;
    const gchar *value;

    if (metadata == NULL) {
        fprintf (stderr, "Error: invalid pointer to metadata reference\n");
        return NULL;
    }

    switch (metadata->operator) {
        case METADATA_OPERATOR_IS_EQUAL:
            operator = "=";
            break;
        case METADATA_OPERATOR_IS_NOT_EQUAL:
            operator = "!=";
            break;
        default:
            return NULL;
    }

    if (metadata->fixed_value != NULL) {
        ret = g_strdup_printf ("( %s %s \"%s\"^^<http://www.w3.org/2001/XMLSchema#string> )",
                               STATIC_METADATA_IDENTIFIER (metadata->metadata), operator, metadata->fixed_value);
    }
    else if (metadata->get_from_self != NULL) {
        ret = g_strdup_printf ("( %s %s %s )", STATIC_METADATA_IDENTIFIER (metadata->metadata), operator,
                               STATIC_METADATA_IDENTIFIER (metadata->get_from_self));
    }
    else if (metadata->get_from_parent != NULL) {
        value = guglielmo_item_get_string_metadata (ref_node->item, STATIC_METADATA_IDENTIFIER (metadata->get_from_parent));

        if (value != NULL) {
            ret = g_strdup_printf ("( %s %s \"%s\"^^<http://www.w3.org/2001/XMLSchema#string> )",
                                   STATIC_METADATA_IDENTIFIER (metadata->metadata), operator, value);
        }
        else {
            fprintf (stderr, "Error: unable to retrieve metadata '%s' in parent node\n", metadata->get_from_parent);
            ret = NULL;
        }
    }
    else
        ret = NULL;

    return ret;
}

static gchar* metadata_reference_with_multiple_options_to_query (MetadataReference *metadata, GList *options)
{
    int total_len;
    int num;
    gchar *subquery;
    gchar *ptr;
    gchar *complete_query;
    GList *iter;
    GList *tokens_list;
    WrappedItem *ref_node;

    tokens_list = NULL;
    total_len = 0;
    num = 0;

    for (iter = options; iter; iter = g_list_next (iter)) {
        ref_node = (WrappedItem*) iter->data;
        subquery = metadata_reference_to_query (metadata, ref_node);
        total_len = total_len + strlen (subquery);
        tokens_list = g_list_prepend (tokens_list, subquery);
        num++;
    }

    /*
        Mainly inspired by implementation of g_strjoinv()
    */

    /*
        Sum of lengths of subqueries + ((number of subqueries - 1) * strlen (" OR ")) + margin
    */
    complete_query = g_new0 (gchar, total_len + ((num - 1) * 4) + 10);
    ptr = g_stpcpy (complete_query, tokens_list->data);

    for (iter = g_list_next (tokens_list); iter; iter = g_list_next (iter)) {
        ptr = g_stpcpy (ptr, " OR ");
        ptr = g_stpcpy (ptr, (gchar*) iter->data);
        g_free (iter->data);
    }

    g_list_free (tokens_list);
    return complete_query;
}

static gchar* condition_policy_to_query (HierarchyNode *hierarchy, ConditionPolicy *cond, WrappedItem *ref_node)
{
    int total_len;
    int num;
    gchar *subquery;
    gchar *ptr;
    gchar *complete_query;
    GList *tokens_list;
    GList *iter;
    MetadataReference *metadata_cond;

    tokens_list = NULL;
    total_len = 0;
    num = 0;

    /**
        TODO    Perhaps this has not to be here
    */
    if (cond->inherit) {
        subquery = condition_policy_to_query (hierarchy->parent, &(hierarchy->parent->child_policy), ref_node->parent);

        if (subquery != NULL && subquery [0] != '\0') {
            total_len = total_len + strlen (subquery);
            tokens_list = g_list_prepend (tokens_list, subquery);
            num++;
        }
    }

    for (iter = cond->conditions; iter; iter = g_list_next (iter)) {
        metadata_cond = (MetadataReference*) iter->data;

        /*
            If the condition involves matching with a metadata from the parent node, but no
            parent node is supplied to this function, this provides to build a complex query
            where all metadata from all upper level contents are exclusively valid and
            concatenated in the final query with OR. This way all possible values are considered
        */
        if (metadata_reference_involves_parent (metadata_cond) == TRUE && ref_node == NULL)
            subquery = metadata_reference_with_multiple_options_to_query (metadata_cond, hierarchy->parent->contents);
        else
            subquery = metadata_reference_to_query (metadata_cond, ref_node);

        if (subquery == NULL || subquery [0] == '\0')
            continue;

        total_len = total_len + strlen (subquery);
        tokens_list = g_list_prepend (tokens_list, subquery);
        num++;
    }

    if (tokens_list != NULL) {
        /*
            Sum of lengths of subqueries + ((number of subqueries - 1) * strlen (" AND ")) + margin
        */
        complete_query = g_new0 (gchar, total_len + ((num - 1) * 5) + 10);
        ptr = g_stpcpy (complete_query, tokens_list->data);

        for (iter = g_list_next (tokens_list); iter; iter = g_list_next (iter)) {
            ptr = g_stpcpy (ptr, " AND ");
            ptr = g_stpcpy (ptr, (gchar*) iter->data);
            g_free (iter->data);
        }

        g_list_free (tokens_list);
    }
    else
        complete_query = NULL;

    return complete_query;
}

static GList* wrapped_free_search (gchar *query)
{
    GList *items;
    GList *iter;
    GList *ret;
    WrappedItem *witem;

    items = guglielmo_core_free_search (query, GUGLIELMO_ITEM_TYPE);
    ret = NULL;

    for (iter = items; iter; iter = g_list_next (iter)) {
        witem = g_new0 (WrappedItem, 1);
        witem->type = REGULAR_ITEM;
        witem->item = (Item*) iter->data;
        ret = g_list_prepend (ret, witem);
    }

    g_list_free (items);
    return g_list_reverse (ret);
}

static GList* wrapped_real_folder (WrappedItem *parent, HierarchyNode *node, GList *existing)
{
    int n;
    register int i;
    gchar *path;
    GList *ret;
    struct dirent **namelist;
    WrappedItem *witem;

    if (node->mapped_folder == NULL) {
        fprintf (stderr, "Error: mapped folder unset\n");
        return NULL;
    }

    ret = NULL;

    if (parent != NULL)
        path = g_build_filename (node->mapped_folder, get_exposed_name (parent), NULL);
    else
        path = g_strdup (node->mapped_folder);

    check_and_create_folder (path);
    n = scandir (path, &namelist, 0, alphasort);

    for (i = 2; i < n; i++) {
        if (check_named_item_in_list (existing, namelist [i]->d_name) == NULL) {
            witem = g_new0 (WrappedItem, 1);
            witem->type = FILESYSTEM_WRAP;
            witem->exposed_name = g_strdup (namelist [i]->d_name);
            witem->real_path = g_build_filename (path, namelist [i]->d_name, NULL);
            ret = g_list_prepend (ret, witem);
        }

        free (namelist [i]);
    }

    free (namelist [0]);
    free (namelist [1]);
    free (namelist);
    g_free (path);
    return g_list_reverse (ret);
}

static void wire_list_wrapped_items (GList *items, HierarchyNode* node, WrappedItem *parent)
{
    GList *iter;

    if (node->contents != NULL) {
        for (iter = node->contents; iter; iter = g_list_next (iter))
            free_wrapped_item ((WrappedItem*) iter->data);
        node->contents = NULL;
    }

    for (iter = items; iter; iter = g_list_next (iter))
        wire_wrapped_item ((WrappedItem*) iter->data, node, parent);
}

static void fill_subcontents_of_hierarchy_node (HierarchyNode* node, WrappedItem *root)
{
    gchar *master_query;
    gchar *child_query;
    gchar *complete_query;
    gboolean master_query_valid;
    GList *iter;
    GList *items;
    GList *collected;
    WrappedItem *temp_item;
    HierarchyNode *master;
    HierarchyNode *child;

    if (root != NULL)
        master = root->hierarchy_node;
    else
        master = node;

    master_query = condition_policy_to_query (master, &(master->child_policy), root);
    master_query_valid = (master_query != NULL && master_query [0] != '\0');
    collected = NULL;

    for (iter = master->children; iter; iter = g_list_next (iter)) {
        child = (HierarchyNode*) iter->data;
        items = NULL;

        if (child->type == HIERARCHY_NODE_IS_STATIC_FOLDER) {
            temp_item = g_new0 (WrappedItem, 1);
            temp_item->type = ABSTRACT_ITEM;
            temp_item->exposed_name = g_strdup (child->expose_policy.formula);
            items = g_list_prepend (items, temp_item);
        }
        else if (child->type == HIERARCHY_NODE_IS_SHADOW_FOLDER) {
            items = wrapped_real_folder (root, child, collected);
        }
        else {
            child_query = condition_policy_to_query (child, &(child->self_policy), root);

            if (child_query == NULL || child_query [0] == '\0') {
                complete_query = g_strdup (master_query);
            }
            else {
                if (master_query_valid == TRUE && child->self_policy.inherit == TRUE)
                    complete_query = g_strdup_printf ("%s AND %s", master_query, child_query);
                else
                    complete_query = g_strdup (child_query);
            }

            items = wrapped_free_search (complete_query);
            g_free (complete_query);

            if (child_query != NULL)
                g_free (child_query);
        }

        if (items != NULL) {
            wire_list_wrapped_items (items, child, root);

            if (collected == NULL)
                collected = items;
            else
                collected = g_list_concat (collected, items);
        }
    }

    g_list_free (collected);
    g_free (master_query);
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

        if (tmp == NULL || strcmp (tmp, ".") == 0) {
            if (tmp != NULL)
                g_free (tmp);
            break;
        }

        g_free (dirs);
        dirs = tmp;
    }

    return list;
}

static inline gboolean hierarchy_node_is_leaf (HierarchyNode *node)
{
    return (node->type == HIERARCHY_NODE_IS_ITEM);
}

static WrappedItem* look_for_level_contents_by_name (HierarchyNode* root, WrappedItem *parent, const gchar *name)
{
    GList *iter;
    WrappedItem *tmp;
    HierarchyNode *node;

    /**
        TODO    This can be substituted with the build and execution of a more efficient query
                based on exposing criteria
    */

    fill_subcontents_of_hierarchy_node (root, parent);
    tmp = NULL;

    if (parent == NULL) {
        for (iter = root->children; iter; iter = g_list_next (iter)) {
            node = (HierarchyNode*) iter->data;
            tmp = check_named_item_in_list (node->contents, name);
            if (tmp != NULL)
                break;
        }
    }
    else {
        tmp = check_named_item_in_list (parent->children, name);
    }

    return tmp;
}

static WrappedItem* retrieve_name_in_real_filesystem (HierarchyNode *node, GList *path)
{
    int total_len;
    int num;
    int strsize;
    gchar *complete_path;
    gchar *name;
    gchar *ptr;
    GList *iter;
    WrappedItem *ret;

    total_len = 0;
    num = 0;

    for (iter = path; iter; iter = g_list_next (iter)) {
        total_len = total_len + strlen ((gchar*) iter->data);
        num++;
    }

    strsize = strlen (node->mapped_folder) + total_len + num + 10;
    complete_path = alloca (strsize);
    memset (complete_path, 0, strsize);
    ptr = complete_path + snprintf (complete_path, strsize, "%s", node->mapped_folder);

    for (iter = path; iter; iter = g_list_next (iter)) {
        ptr = g_stpcpy (ptr, "/");
        name = (gchar*) iter->data;
        ptr = g_stpcpy (ptr, name);
    }

    if (access (complete_path, F_OK) == 0) {
        ret = g_new0 (WrappedItem, 1);
        ret->type = FILESYSTEM_WRAP;
        ret->exposed_name = g_strdup (name);
        ret->real_path = g_strdup (complete_path);
        ret->hierarchy_node = node;
        return ret;
    }
    else {
        return NULL;
    }
}

HIERARCHY_LEVEL verify_exposed_path (const gchar *path, WrappedItem **target, gchar *name)
{
    char *home;
    char *ref;
    gboolean into_home;
    int home_len;
    GList *path_tokens;
    GList *iter;
    HierarchyNode *level;
    WrappedItem *item;
    WrappedItem *next_item;
    HIERARCHY_LEVEL ret;

    *target = NULL;
    ret = IS_ARBITRARY_PATH;

    if (name != NULL)
        memset (name, 0, MAX_NAMES_SIZE);

    home = getenv("HOME");
    if (home == NULL) {
        return IS_ARBITRARY_PATH;
    }

    home_len = strlen (home);

    /**
        TODO    Warning: here we handle / just as /home/user, so to display Containers also in
                the root folder (required by Lorenzo - 10.07.2009). This behaviour may change in
                future
    */

    if (strncmp(path, home, home_len) == 0) {
        if (path [home_len] == '\0')
            return IS_IN_HOME_FOLDER;

        into_home = TRUE;
        ref = strdupa (path + home_len + 1);
    }
    else {
        if (strcmp (path, "/") == 0)
            return IS_IN_HOME_FOLDER;

        into_home = FALSE;
        ref = strdupa (path + 1);
    }

    if (ExposingTree == NULL) {
        fprintf (stderr, "Warning: there is not an exposing hierarchy\n");
        ret = IS_ARBITRARY_PATH;
    }
    else {
        path_tokens = tokenize_path (ref);

        item = NULL;
        iter = NULL;
        next_item = NULL;
        level = ExposingTree;

        for (iter = path_tokens; iter; iter = g_list_next (iter)) {
            next_item = look_for_level_contents_by_name (level, item, (const gchar*) iter->data);
            if (next_item == NULL)
                break;

            level = next_item->hierarchy_node;

            /*
                When a child of a "system folders" node is required, it is managed in special way: its path is
                directly accessed to check its existance, and a dummy WrappedItem is returned. This is to avoid to
                dump into the hierarchy tree the whole real filesystem and speed up legacy operations
            */
            if (next_item->type == FILESYSTEM_WRAP && iter->next != NULL) {
                item = retrieve_name_in_real_filesystem (level, iter);
                break;
            }
            else {
                item = next_item;
            }
        }

        if (item == NULL || (next_item == NULL && level == NULL)) {
            if (g_list_length (path_tokens) == 1) {
                ret = IS_MISSING_HIERARCHY_LEAF;
                *target = ExposingTree->contents->data;
            }
            else if (next_item->type == FILESYSTEM_WRAP) {
                ret = IS_MISSING_HIERARCHY_EXTENSION;
                *target = next_item;
            }
            else {
                ret = IS_ARBITRARY_PATH;
            }
        }
        else if (next_item == NULL) {
            /*
                If no element is found, it is a problem to understand if the intention was to
                retrieve an Item or a folder in the hierarchy, so IS_MISSING_HIERARCHY_LEAF may means
                both
            */

            if (name != NULL)
                snprintf (name, MAX_NAMES_SIZE, "%s", (const gchar*) iter->data);

            *target = item;
            ret = IS_MISSING_HIERARCHY_LEAF;
        }
        else if (next_item->type == FILESYSTEM_WRAP) {
            *target = item;
            ret = IS_HIERARCHY_EXTENSION;
        }
        else if (hierarchy_node_is_leaf (item->hierarchy_node) == TRUE) {
            *target = item;
            ret = IS_HIERARCHY_LEAF;
        }
        else if (hierarchy_node_is_leaf (item->hierarchy_node) == FALSE) {
            *target = item;
            ret = IS_HIERARCHY_FOLDER;
        }

        easy_list_free (path_tokens);
    }

    return ret;
}

void hierarchy_foreach_content (WrappedItem *root, HierarchyForeachCallback callback, gpointer userdata)
{
    GList *iter;
    GList *inner_iter;
    WrappedItem *item;
    HierarchyNode *node;

    if (root == NULL) {
        fill_subcontents_of_hierarchy_node (ExposingTree, NULL);

        for (iter = ExposingTree->children; iter; iter = g_list_next (iter)) {
            node = (HierarchyNode*) iter->data;

            if (node->contents == NULL)
                fill_subcontents_of_hierarchy_node (node, NULL);

            for (inner_iter = node->contents; inner_iter; inner_iter = g_list_next (inner_iter)) {
                item = (WrappedItem*) inner_iter->data;

#if 0
                /*
                    Real filesystem wrappers are not directly exposed, they are hidden and
                    accessed only when explicitely required
                */
                if (item->type == FILESYSTEM_WRAP)
                    continue;
#endif

                if (callback (item, userdata) == FALSE)
                    break;
            }
        }
    }

    else {
        if (root->children == NULL)
            fill_subcontents_of_hierarchy_node (NULL, root);

        for (iter = root->children; iter; iter = g_list_next (iter)) {
            item = (WrappedItem*) iter->data;

            if (item->type == FILESYSTEM_WRAP)
                continue;

            if (callback (item, userdata) == FALSE)
                break;
        }
    }
}

static gboolean guess_metadata_from_saving_name (WrappedItem *item, HierarchyNode *parent, const gchar *name)
{
    int num;
    int i;
    int valid_extracted_senteces;
    int compilation;
    gchar *str;
    gboolean metadata_set;
    GList *metadata_iter;
    GList *metadata_offset;
    regex_t reg;
    regmatch_t *matches;
    MetadataReference *metadata_ref;
    SaveExtractName *policy;

    policy = &(parent->save_policy.extraction_behaviour);

    if (policy->formula == NULL) {
        fprintf (stderr, "Error: missing exposing formula\n");
        return FALSE;
    }

    num = g_list_length (policy->assigned_metadata) + 1;
    matches = alloca (sizeof (regmatch_t) * num);
    memset (matches, 0, sizeof (regmatch_t) * num);

    compilation = regcomp (&reg, policy->formula, REG_EXTENDED);

    if (compilation != 0) {
        fprintf (stderr, "Unable to compile matching regular expression: %d\n", compilation);
    }
    else {
        if (regexec (&reg, name, num, matches, 0) == REG_NOMATCH) {
            fprintf (stderr, "Unable to match expression while parsing new file's name\n");
        }
        else {
            metadata_offset = policy->assigned_metadata;
            valid_extracted_senteces = 0;

            /*
                In matches[0] there is the whole string match, to skip
            */
            for (i = 1; i < num && matches [i].rm_so != -1; i++) {
                str = g_strndup (name + matches [i].rm_so, matches [i].rm_eo);

                metadata_set = FALSE;
                valid_extracted_senteces++;

                for (metadata_iter = metadata_offset; metadata_iter; metadata_iter = g_list_next (metadata_iter)) {
                    metadata_ref = (MetadataReference*) metadata_iter->data;

                    if (metadata_ref->get_from_extraction == i) {
                        item_set_metadata (item->item, metadata_ref->metadata, str, TRUE);
                        metadata_set = TRUE;

                        /*
                            This truly depends on sorting acted in parse_saving_policy()
                        */
                        metadata_offset = g_list_next (metadata_iter);
                        break;
                    }
                }

                if (metadata_set == FALSE)
                    g_free (str);
            }
        }
    }

    for (metadata_iter = policy->assigned_metadata; metadata_iter; metadata_iter = g_list_next (metadata_iter)) {
        metadata_ref = (MetadataReference*) metadata_iter->data;

        if (metadata_ref->fixed_value != NULL) {
            if (metadata_ref->condition_from_extraction != 0) {
                i = metadata_ref->condition_from_extraction;
                if (i > valid_extracted_senteces || (matches [i].rm_so == matches [i].rm_eo))
                    continue;
            }

            item_set_metadata (item->item, metadata_ref->metadata, metadata_ref->fixed_value, FALSE);
        }

        /**
            TODO    Has this to be completed with exotic metadata assignments handling?
        */
    }

    return TRUE;
}

static void attach_inherit_metadata (WrappedItem *item, HierarchyNode *node, WrappedItem *parent)
{
    const gchar *val;
    GList *iter;
    MetadataReference *metadata;

    if (node == NULL) {
        fprintf (stderr, "Error: parent node is not set\n");
    }
    else {
        for (iter = node->save_policy.inheritable_assignments; iter; iter = g_list_next (iter)) {
            metadata = (MetadataReference*) iter->data;

            if (metadata->fixed_value != NULL) {
                item_set_metadata (item->item, metadata->metadata, metadata->fixed_value, FALSE);
            }
            else if (metadata->get_from_self != NULL) {
                val = guglielmo_item_get_string_metadata (parent->item, STATIC_METADATA_IDENTIFIER (metadata->get_from_self));
                if (val != NULL)
                    item_set_metadata (item->item, metadata->metadata, (gchar*) val, FALSE);
            }
            else if (metadata->get_from_parent != NULL) {
                if (parent == NULL) {
                    fprintf (stderr, "Error: required a metadata from parent, but parent node is not set\n");
                }
                else {
                    val = guglielmo_item_get_string_metadata (parent->parent->item, STATIC_METADATA_IDENTIFIER (metadata->get_from_parent));
                    if (val != NULL)
                        item_set_metadata (item->item, metadata->metadata, (gchar*) val, FALSE);
                }
            }
        }

        if (node->save_policy.inherit == TRUE && node->parent != NULL)
            attach_inherit_metadata (item, node->parent, parent ? parent->parent : NULL);
    }
}

static void calculate_real_path (HierarchyNode *node, const gchar *required_path, WrappedItem *item, NODE_TYPE type)
{
    gchar *subpath;
    gchar *path;
    gchar *temp;

    if (node->save_policy.hijack_folder == NULL) {
        /**
            TODO    Retrieve the real_path from saving hierarchy
        */

        temp = g_strdup (FAKE_SAVING_FOLDER "/XXXXXX");
        mktemp (temp);
    }
    else {
        subpath = g_path_get_dirname (required_path);
        path = g_build_filename (node->save_policy.hijack_folder, subpath, NULL);

        check_and_create_folder (path);
        g_free (subpath);
        g_free (path);

        temp = g_build_filename (node->save_policy.hijack_folder, required_path, NULL);
    }

    if (type == NODE_FOLDER) {
        mkdir (temp, 0770);
    }
    else if (type == NODE_FILE || type == NODE_ITEM) {
        fclose (fopen (temp, "w"));
    }

    if (item->real_path != NULL)
        g_free (item->real_path);
    item->real_path = temp;

    if (item->item != NULL) {
        /*
            The string assigned as value for the real path metadata must be an URI, to be
            correctly handled by the metadata extractor
        */
        path = g_strdup_printf ("file://%s", temp);
        guglielmo_item_set_metadata (item->item, REAL_PATH_METADATA, METADATA_VALUE_FROM_STATIC_STRING (path));
    }
}

static WrappedItem* retrieve_item_by_metadata_value (GList *items, gchar *metadata, const gchar *value)
{
    const gchar *this_value;
    GList *iter;
    WrappedItem *tmp;

    for (iter = items; iter; iter = g_list_next (iter)) {
        tmp = (WrappedItem*) iter->data;
        this_value = guglielmo_item_get_string_metadata (tmp->item, STATIC_METADATA_IDENTIFIER (metadata));
        if (this_value != NULL && strcmp (value, this_value) == 0)
            return tmp;
    }

    return NULL;
}

static void dispose_wrapped_item_in_hierarchy (HierarchyNode *node, WrappedItem *item)
{
    const gchar *value;
    const gchar *other_value;
    gboolean include;
    GList *iter;
    WrappedItem *parent_item;
    MetadataReference *cond;
    HierarchyNode *subnode;

    include = TRUE;
    parent_item = NULL;

    for (iter = node->self_policy.conditions; iter; iter = g_list_next (iter)) {
        cond = (MetadataReference*) iter->data;
        value = guglielmo_item_get_string_metadata (item->item, STATIC_METADATA_IDENTIFIER (cond->metadata));

        if (value == NULL) {
            include = FALSE;
            break;
        }

        if (cond->fixed_value != NULL && strcmp (value, cond->fixed_value) != 0) {
            include = FALSE;
            break;
        }
        else if (cond->get_from_self != NULL) {
            other_value = guglielmo_item_get_string_metadata (item->item, STATIC_METADATA_IDENTIFIER (cond->get_from_self));

            if (other_value == NULL || strcmp (value, other_value) != 0) {
                include = FALSE;
                break;
            }
        }
        else if (cond->get_from_parent != NULL) {
            parent_item = retrieve_item_by_metadata_value (node->parent->contents, cond->get_from_parent, value);

            if (parent_item == NULL) {
                include = FALSE;
                break;
            }
        }
    }

    if (include == TRUE) {
        if (node->contents == NULL) {
            node->contents = g_list_prepend (node->contents, item);
        }
        else {
            node->contents = g_list_append (node->contents, item);
        }

        item->hierarchy_node = node;

        if (parent_item != NULL) {
            if (parent_item->children == NULL) {
                parent_item->children = g_list_prepend (parent_item->children, item);
            }
            else {
                parent_item->children = g_list_append (parent_item->children, item);
            }
        }
    }

    for (iter = node->children; iter; iter = g_list_next (iter)) {
        subnode = (HierarchyNode*) iter->data;
        dispose_wrapped_item_in_hierarchy (subnode, item);
    }
}

gboolean modify_hierarchy_node (WrappedItem *node, WrappedItem *parent, const gchar *destination)
{
    gchar *name;
    const gchar *ex_path;
    Item *ex_item;
    NODE_TYPE type;

    if (parent->hierarchy_node->save_policy.writable == FALSE) {
        fprintf (stderr, "Not a writable path in node %s\n", parent->hierarchy_node->name);
        return FALSE;
    }
    else {
        name = g_path_get_basename (destination);

        if (parent->hierarchy_node->save_policy.hijack_folder != NULL) {
            ex_path = get_real_path (node);
            if (ex_path != NULL)
                ex_path = strdupa (ex_path);

            node->type = FILESYSTEM_WRAP;

            if (node->exposed_name != NULL)
                g_free (node->exposed_name);
            node->exposed_name = name;

            if (node->real_path != NULL) {
                g_free (node->real_path);
                node->real_path = NULL;
            }

            ex_item = NULL;

            /*
                The item into the node is invalidated so to permit an effective real path
                calculation, but it is not destroyed because guglielmo_core_destroy_item() also
                removes the real file which has to be instead moved in the new path
            */
            if (node->item != NULL) {
                type = get_element_type (node);
                ex_item = node->item;
                node->item = NULL;
            }
            else {
                type = NODE_INVALID;
            }

            calculate_real_path (parent->hierarchy_node, destination, node, type);

            if (ex_path != NULL) {
                rename (ex_path, get_real_path (node));
            }

            if (ex_item != NULL) {
                guglielmo_core_destroy_item (ex_item);
                g_object_unref (ex_item);
            }
        }
        else {
            node->type = REGULAR_ITEM;

            if (node->item == NULL)
                node->item = guglielmo_item_alloc ();

            guess_metadata_from_saving_name (node, parent->hierarchy_node, name);
            attach_inherit_metadata (node, parent->hierarchy_node, parent);
            dispose_wrapped_item_in_hierarchy (ExposingTree, node);
            g_free (name);
        }

        return TRUE;
    }
}

void remove_hierarchy_node (WrappedItem *node)
{
    if (node->type == FILESYSTEM_WRAP) {
        if (node->real_path != NULL)
            remove (node->real_path);
    }
    else if (node->type == REGULAR_ITEM) {
        if (node->item != NULL) {
            guglielmo_core_destroy_item (node->item);
            g_object_unref (node->item);
            node->item = NULL;
        }
    }

    free_wrapped_item (node);
}

WrappedItem* create_hierarchy_node (WrappedItem *parent, const gchar *path, NODE_TYPE type)
{
    WrappedItem *tmp;

    tmp = g_new0 (WrappedItem, 1);

    if (modify_hierarchy_node (tmp, parent, path) == FALSE) {
        free_wrapped_item (tmp);
        g_free (tmp);
        tmp = NULL;
    }
    else {
        calculate_real_path (parent->hierarchy_node, path, tmp, type);
    }

    return tmp;
}

int hierarchy_node_open (WrappedItem *node, int flags)
{
    int fd;

    if (node->hierarchy_node != NULL && node->hierarchy_node->expose_policy.contents_callback != NULL) {
        fd = node->hierarchy_node->expose_policy.contents_callback (&(node->hierarchy_node->expose_policy), node, flags);
    }
    else if (node->item != NULL) {
        fd = guglielmo_item_open (node->item, flags);
    }
    else if (node->real_path != NULL) {
        fd = open (node->real_path, flags);
    }
    else {
        fd = -1;
    }

    return fd;
}

void hierarchy_node_close (WrappedItem *node, int fd, gboolean written)
{
    if (node->hierarchy_node != NULL && node->hierarchy_node->expose_policy.contents_callback != NULL)
        close (fd);
    else if (node->item != NULL)
        guglielmo_item_close (node->item, fd, written);
    else
        close (fd);
}
