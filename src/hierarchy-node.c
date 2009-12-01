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

#include "hierarchy-node.h"
#include "property-handler.h"
#include "hierarchy.h"
#include "nodes-cache.h"
#include "utils.h"

#define HIERARCHY_NODE_GET_PRIVATE(obj)     (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HIERARCHY_NODE_TYPE, HierarchyNodePrivate))

typedef struct _ExposePolicy            ExposePolicy;
typedef int (*ContentCallback)          (ExposePolicy *policy, ItemHandler *item, int flags);

typedef enum {
    METADATA_OPERATOR_IS_EQUAL,
    METADATA_OPERATOR_IS_NOT_EQUAL,
} METADATA_OPERATOR;

typedef struct {
    PropertyHandler     *metadata;
    gboolean            means_subject;
    gchar               *get_from_parent;
    gchar               *get_from_self;
    gchar               *fixed_value;
    gchar               *comparison_value;
    METADATA_OPERATOR   operator;
    int                 get_from_extraction;
    int                 condition_from_extraction;
} ValuedMetadataReference;

struct _ExposePolicy {
    gchar               *formula;
    GList               *exposed_metadata;          // list of PropertyHandler
    GList               *conditional_metadata;      // list of ValuedMetadataReference
    gchar               *content_metadata;
    ContentCallback     contents_callback;
};

typedef struct {
    gboolean            inherit;
    GList               *conditions;                // list of ValuedMetadataReference
} ConditionPolicy;

typedef struct {
    gchar               *formula;
    GList               *assigned_metadata;         // list of ValuedMetadataReference
} SaveExtractName;

typedef struct {
    gboolean            inherit;
    gboolean            writable;
    GList               *inheritable_assignments;   // list of ValuedMetadataReference
    SaveExtractName     extraction_behaviour;
    gchar               *hijack_folder;
} SavePolicy;

struct _HierarchyNodePrivate {
    CONTENT_TYPE        type;
    gchar               *name;
    HierarchyNode       *node;

    gchar               *mapped_folder;
    SavePolicy          save_policy;
    ExposePolicy        expose_policy;
    ConditionPolicy     self_policy;
    ConditionPolicy     child_policy;

    GList               *children;
};

enum {
    PROP_0,
    PROP_NODE
};

struct {
    const gchar         *tag;
    CONTENT_TYPE        type;
    gboolean            has_base_path;
} HierarchyDescription [] = {
    { "root",             ITEM_IS_ROOT,             FALSE },
    { "file",             ITEM_IS_VIRTUAL_ITEM,     FALSE },
    { "folder",           ITEM_IS_VIRTUAL_FOLDER,   FALSE },
    { "static_folder",    ITEM_IS_STATIC_FOLDER,    FALSE },
    { "shadow_content",   ITEM_IS_SHADOW_FOLDER,    TRUE },
    { NULL,               0,                        FALSE }
};

G_DEFINE_TYPE (HierarchyNode, hierarchy_node, G_TYPE_OBJECT);

static void free_metadata_reference (ValuedMetadataReference *ref)
{
    g_object_unref (ref->metadata);

    if (ref->get_from_parent != NULL)
        g_free (ref->get_from_parent);
    if (ref->get_from_self != NULL)
        g_free (ref->get_from_self);
    if (ref->fixed_value != NULL)
        g_free (ref->fixed_value);

    g_free (ref);
}

static void free_expose_policy (ExposePolicy *policy)
{
    GList *iter;

    if (policy->formula != NULL)
        g_free (policy->formula);

    for (iter = policy->exposed_metadata; iter; iter = g_list_next (iter))
        g_object_unref ((PropertyHandler*) iter->data);

    if (policy->content_metadata != NULL)
        g_free (policy->content_metadata);
}

static void free_condition_policy (ConditionPolicy *policy)
{
    GList *iter;

    for (iter = policy->conditions; iter; iter = g_list_next (iter))
        free_metadata_reference ((ValuedMetadataReference*) iter->data);
}

static void free_save_policy (SavePolicy *policy)
{
    GList *iter;

    for (iter = policy->inheritable_assignments; iter; iter = g_list_next (iter))
        free_metadata_reference ((ValuedMetadataReference*) iter->data);

    for (iter = policy->extraction_behaviour.assigned_metadata; iter; iter = g_list_next (iter))
        free_metadata_reference ((ValuedMetadataReference*) iter->data);

    if (policy->hijack_folder != NULL)
        g_free (policy->hijack_folder);

    if (policy->extraction_behaviour.formula != NULL)
        g_free (policy->extraction_behaviour.formula);
}

static void hierarchy_node_finalize (GObject *obj)
{
    GList *iter;
    HierarchyNode *node;

    node = HIERARCHY_NODE (obj);

    if (node->priv->mapped_folder != NULL)
        g_free (node->priv->mapped_folder);

    free_expose_policy (&(node->priv->expose_policy));
    free_condition_policy (&(node->priv->self_policy));
    free_condition_policy (&(node->priv->child_policy));
    free_save_policy (&(node->priv->save_policy));

    for (iter = node->priv->children; iter; iter = g_list_next (iter))
        g_object_unref ((HierarchyNode*) iter->data);
}

static void hierarchy_node_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    HierarchyNode *self = HIERARCHY_NODE (object);

    switch (property_id) {
        case PROP_NODE:
            self->priv->node = g_value_dup_object (value);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void hierarchy_node_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    HierarchyNode *self = HIERARCHY_NODE (object);

    switch (property_id) {
        case PROP_NODE:
            g_value_set_object (value, self->priv->node);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void hierarchy_node_class_init (HierarchyNodeClass *klass)
{
    GObjectClass *gobject_class;
    GParamSpec *param_spec;

    g_type_class_add_private (klass, sizeof (HierarchyNodePrivate));

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->finalize = hierarchy_node_finalize;
    gobject_class->set_property = hierarchy_node_set_property;
    gobject_class->get_property = hierarchy_node_get_property;

    param_spec = g_param_spec_object ("node",
                                        "Hierarchy node",
                                        "Hierarchy node from which this one depends",
                                        HIERARCHY_NODE_TYPE,
                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    g_object_class_install_property (gobject_class, PROP_NODE, param_spec);
}

static void hierarchy_node_init (HierarchyNode *node)
{
    node->priv = HIERARCHY_NODE_GET_PRIVATE (node);
    memset (node->priv, 0, sizeof (HierarchyNodePrivate));
}

static ValuedMetadataReference* parse_reference_to_metadata (const gchar *tag, xmlNode *node)
{
    gchar *str;
    ValuedMetadataReference *ref;

    if (strcmp ((gchar*) node->name, tag) != 0) {
        g_warning ("Error: expected '%s', found '%s'", tag, node->name);
        return NULL;
    }

    str = (gchar*) xmlGetProp (node, (xmlChar*) "metadata");
    if (str == NULL) {
        g_warning ("Error: 'metadata' tag required in definition");
        return NULL;
    }

    ref = g_new0 (ValuedMetadataReference, 1);
    ref->metadata = properties_pool_get (str);

    str = (gchar*) xmlGetProp (node, (xmlChar*) "operator");
    if (str != NULL) {
        if (strcmp (str, "is") == 0)
            ref->operator = METADATA_OPERATOR_IS_EQUAL;
        else if (strcmp (str, "isnot") == 0)
            ref->operator = METADATA_OPERATOR_IS_NOT_EQUAL;
        xmlFree (str);
    }
    else {
        ref->operator = METADATA_OPERATOR_IS_EQUAL;
    }

    ref->fixed_value = (gchar*) xmlGetProp (node, (xmlChar*) "value");

    if (ref->fixed_value == NULL) {
        str = NULL;
        ref->get_from_self = (gchar*) xmlGetProp (node, (xmlChar*) "valuefrommetadata");

        if (ref->get_from_self == NULL) {
            ref->get_from_parent = (gchar*) xmlGetProp (node, (xmlChar*) "valuefromparentmetadata");

            if (ref->get_from_parent == NULL) {
                str = (gchar*) xmlGetProp (node, (xmlChar*) "valuefromextract");

                if (str != NULL) {
                    ref->get_from_extraction = strtoull (str, NULL, 10);
                    xmlFree (str);
                }
                else {
                    g_warning ("Error: unrecognized metadata assignment behaviour in %s", (gchar*) node->name);
                    free_metadata_reference (ref);
                    ref = NULL;
                }
            }
            else {
                str = ref->get_from_parent;
            }
        }
        else {
            str = ref->get_from_self;
        }

        /*
            This is to handle the special identifier "/subject", used to
            refere to a subject instead than a specific property
        */
        if (str != NULL && strcmp (str, "/subject") == 0)
            ref->means_subject = TRUE;
    }

    ref->comparison_value = (gchar*) xmlGetProp (node, (xmlChar*) "ifmetadatavalue");

    str = (gchar*) xmlGetProp (node, (xmlChar*) "iffromextract");
    if (str != NULL) {
        ref->condition_from_extraction = strtoull (str, NULL, 10);
        xmlFree (str);
    }

    str = (gchar*) xmlGetProp (node, (xmlChar*) "id");
    if (str != NULL) {
        ref->condition_from_extraction = strtoull (str, NULL, 10);
        xmlFree (str);
    }

    return ref;
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

static gint sort_extraction_metadata (gconstpointer a, gconstpointer b)
{
    ValuedMetadataReference *first;
    ValuedMetadataReference *second;

    first = (ValuedMetadataReference*) a;
    second = (ValuedMetadataReference*) b;

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
    xmlNode *node;
    xmlNode *subnode;
    ValuedMetadataReference *ref;

    saving->inherit = TRUE;

    str = (gchar*) xmlGetProp (root, (xmlChar*) "inherit");
    if (str != NULL) {
        if (strcmp (str, "no") == 0)
            saving->inherit = FALSE;
        xmlFree (str);
    }

    ret = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "inheritable_metadatas") == 0) {
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
        else if (strcmp ((gchar*) node->name, "new_file") == 0) {
            saving->extraction_behaviour.formula = (gchar*) xmlGetProp (node, (xmlChar*) "extractmetadata");

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

            if (ret == TRUE)
                saving->extraction_behaviour.assigned_metadata = g_list_sort (saving->extraction_behaviour.assigned_metadata, sort_extraction_metadata);
        }
        else if (strcmp ((gchar*) node->name, "new_shadow_content") == 0) {
            str = (gchar*) xmlGetProp (root, (xmlChar*) "base_path");
            if (str != NULL) {
                saving->hijack_folder = expand_path_to_absolute (str);
                xmlFree (str);
            }
        }
        else {
            g_warning ("Unrecognized saving condition");
        }
    }

    if (ret == TRUE)
        saving->writable = TRUE;

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
            metadata = g_list_prepend (metadata, properties_pool_get (string + a));
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
            g_object_unref ((PropertyHandler*) iter->data);

        g_list_free (metadata);
    }

    return ret;
}

/**
    TODO    This has to be splitted in a dedicated content plugin
*/
static int item_content_plain (ExposePolicy *policy, ItemHandler *item, int flags)
{
    return item_handler_open (item, flags);
}

/**
    TODO    This has to be splitted in a dedicated content plugin
*/

#define METADATA_DUMPS_PATH             "/tmp/.avfs_dumps/"

static int item_content_dump_metadata (ExposePolicy *policy, ItemHandler *item, int flags)
{
    int fd;
    FILE *file;
    gchar *path;
    const gchar *id;
    const gchar *value;
    GList *metadata_list;
    GList *iter;

    if (strcmp (policy->content_metadata, "*") == 0) {
        id = item_handler_exposed_name (item);
        path = g_build_filename (METADATA_DUMPS_PATH, id, NULL);

        /**
            TODO    If dump file already exists it is used, but it is not updated with latest metadata
                    assignments and changes. Provide some kind of check and update
        */
        if (access (path, F_OK) != 0) {
            metadata_list = item_handler_get_all_metadata (item);
            file = fopen (path, "w");

            if (file == NULL) {
                g_warning ("Error dumping metadata: unable to create file in %s, %s", path, strerror (errno));
            }
            else {
                for (iter = metadata_list; iter; iter = g_list_next (iter)) {
                    value = item_handler_get_metadata (item, (gchar*) iter->data);

                    if (value == NULL) {
                        g_warning ("Error dumping metadata: '%s' exists but has no value", (gchar*) iter->data);
                        continue;
                    }

                    fprintf (file, "%s: %s\n", (gchar*) iter->data, value);
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
    ValuedMetadataReference *first;
    ValuedMetadataReference *second;

    first = (ValuedMetadataReference*) a;
    second = (ValuedMetadataReference*) b;

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

static gboolean parse_exposing_policy (ExposePolicy *exposing, xmlNode *root)
{
    gchar *str;
    gboolean ret;
    xmlNode *node;
    xmlNode *subnode;

    ret = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "name") == 0) {
            str = (gchar*) xmlGetProp (node, (xmlChar*) "value");
            if (str != NULL) {
                if (parse_exposing_formula (exposing, str) == FALSE)
                    ret = FALSE;
                xmlFree (str);
            }

            if (node->children != NULL) {
                for (subnode = node->children; subnode; subnode = subnode->next)
                    exposing->conditional_metadata = g_list_prepend (exposing->conditional_metadata,
                                                                     parse_reference_to_metadata ("derivated_value", subnode));
                exposing->conditional_metadata = g_list_sort (exposing->conditional_metadata, sort_conditional_metadata);
            }
        }
        else if (strcmp ((gchar*) node->name, "content") == 0) {
            str = (gchar*) xmlGetProp (node, (xmlChar*) "type");
            if (str != NULL) {
                /**
                    TODO    Handle contents callbacks with an external plugins system
                */
                if (strcmp ((gchar*) str, "real_file") == 0) {
                    exposing->contents_callback = item_content_plain;
                }
                else if (strcmp ((gchar*) str, "dump_metadata") == 0) {
                    exposing->contents_callback = item_content_dump_metadata;
                }
                else {
                    g_warning ("Unable to identify contents exposing policy, found %s", (gchar*) str);
                    ret = FALSE;
                }

                xmlFree (str);
            }

            exposing->content_metadata = (gchar*) xmlGetProp (node, (xmlChar*) "metadata");
        }
        else {
            g_warning ("Unrecognized tag %s", (gchar*) node->name);
        }
    }

    return ret;
}

static gboolean parse_conditions_policy (ConditionPolicy *conditions, xmlNode *root)
{
    gchar *attr;
    gboolean ret;
    GList *cond_list;
    GList *iter;
    xmlNode *node;
    ValuedMetadataReference *cond;

    attr = (gchar*) xmlGetProp (root, (xmlChar*) "from_parent");
    if (attr != NULL) {
        if (strcmp (attr, "no") == 0)
            conditions->inherit = FALSE;
        else
            conditions->inherit = TRUE;
        xmlFree (attr);
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
            free_metadata_reference ((ValuedMetadataReference*) iter->data);
        g_list_free (cond_list);
    }

    return ret;
}

static gboolean parse_content_policy (HierarchyNode *parent, xmlNode *root)
{
    gboolean ret;
    GList *children;
    xmlNode *node;
    HierarchyNode *child;

    children = NULL;
    ret = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "inheritable_conditions") == 0) {
            ret = parse_conditions_policy (&(parent->priv->child_policy), node);
        }
        else {
            child = hierarchy_node_new_from_xml (parent, node);
            if (child != NULL)
                children = g_list_prepend (children, child);
        }
    }

    if (children != NULL)
        parent->priv->children = g_list_reverse (children);

    return ret;
}

static void add_base_path (HierarchyNode *this, xmlNode *root)
{
    gchar *str;

    str = (gchar*) xmlGetProp (root, (xmlChar*) "base_path");
    if (str != NULL) {
        this->priv->mapped_folder = expand_path_to_absolute (str);
        this->priv->save_policy.hijack_folder = g_strdup (this->priv->mapped_folder);
        this->priv->save_policy.writable = TRUE;
        free (str);
    }
}

static gboolean parse_exposing_nodes (HierarchyNode *this, xmlNode *root)
{
    register int i;
    gchar *str;
    gboolean ret;
    xmlNode *node;

    ret = FALSE;

    for (i = 0; HierarchyDescription [i].tag != NULL; i++) {
        if (strcmp (HierarchyDescription [i].tag, (gchar*) root->name) == 0) {
            this->priv->type = HierarchyDescription [i].type;

            if (HierarchyDescription [i].has_base_path == TRUE)
                add_base_path (this, root);

            ret = TRUE;
            break;
        }
    }

    if (ret == FALSE) {
        g_warning ("Error: unrecognized type of node, '%s'", root->name);
    }
    else {
        /*
            The ID for the node is used just for debugging porpuses
        */
        str = (gchar*) xmlGetProp (root, (xmlChar*) "id");
        if (str != NULL)
            this->priv->name = str;

        for (node = root->children; ret == TRUE && node; node = node->next) {
            if (strcmp ((gchar*) node->name, "saving_policy") == 0) {
                ret = parse_saving_policy (&(this->priv->save_policy), node);
            }
            else if (strcmp ((gchar*) node->name, "expose") == 0) {
                ret = parse_exposing_policy (&(this->priv->expose_policy), node);
            }
            else if (strcmp ((gchar*) node->name, "content") == 0) {
                ret = parse_content_policy (this, node);
            }
            else if (strcmp ((gchar*) node->name, "local_conditions") == 0) {
                ret = parse_conditions_policy (&(this->priv->self_policy), node);
            }
            else {
                g_warning ("Unrecognized tag in %s", this->priv->name);
                ret = FALSE;
            }
        }
    }

    return ret;
}

/**
 * hierarchy_node_new_from_xml:
 * @parent: parent of the new hierarchy node, to wire to the new one so to be
 * able to construct complex queries at runtime, or NULL
 * @node: XML node to parse
 *
 * Creates a new node in function of the specified portion of XML. It
 * provides to call recursively itself if other hierarchy nodes are found in
 * configuration
 *
 * Return value: a new #HierarchyNode built from the specified XML
 * configuration
 **/
HierarchyNode* hierarchy_node_new_from_xml (HierarchyNode *parent, xmlNode *node)
{
    HierarchyNode *ret;

    ret = g_object_new (HIERARCHY_NODE_TYPE, "node", parent, NULL);

    if (parse_exposing_nodes (ret, node) == FALSE) {
        g_object_unref (ret);
        ret = NULL;
    }

    return ret;
}

/**
 * hierarchy_node_get_format:
 * @node: a #HierarchyNode
 *
 * To retrieve the type of the @node. From this value depends some behaviour
 *
 * Return value: type of the specified #HierarchyNode
 **/
CONTENT_TYPE hierarchy_node_get_format (HierarchyNode *node)
{
    return node->priv->type;
}

static GList* condition_policy_to_sparql (ConditionPolicy *policy, ItemHandler *parent, int *offset)
{
    int value_offset;
    gchar *stat;
    gchar *val;
    GList *iter;
    GList *statements;
    ValuedMetadataReference *meta_ref;
    CONTENT_TYPE parent_type;

    statements = NULL;
    value_offset = *offset;

    for (iter = policy->conditions; iter; iter = g_list_next (iter)) {
        meta_ref = (ValuedMetadataReference*) iter->data;
        stat = NULL;

        if (meta_ref->fixed_value != NULL) {
            if (meta_ref->operator == METADATA_OPERATOR_IS_EQUAL) {
                val = property_handler_format_value (meta_ref->metadata, meta_ref->fixed_value);
                stat = g_strdup_printf ("?item %s %s", property_handler_get_name (meta_ref->metadata), val);
                g_free (val);
            }
            else {
                val = property_handler_format_value (meta_ref->metadata, meta_ref->fixed_value);
                stat = g_strdup_printf ("?item %s ?var%d . FILTER ( !( ?var%d = %s ) )",
                                        property_handler_get_name (meta_ref->metadata), value_offset, value_offset, val);
                g_free (val);
                value_offset++;
            }
        }
        else if (meta_ref->get_from_self != NULL) {
            if (meta_ref->operator == METADATA_OPERATOR_IS_EQUAL) {
                if (meta_ref->means_subject == TRUE) {
                    stat = g_strdup_printf ("?item %s ?item", property_handler_get_name (meta_ref->metadata));
                }
                else {
                    stat = g_strdup_printf ("?item %s ?var%d . ?item %s ?var%d",
                                            property_handler_get_name (meta_ref->metadata), value_offset,
                                            meta_ref->get_from_self, value_offset);
                    value_offset++;
                }
            }
            else {
                if (meta_ref->means_subject == TRUE) {
                    stat = g_strdup_printf ("?item %s ?var%d . FILTER ( !( ?var%d = ?item ) )",
                                            property_handler_get_name (meta_ref->metadata), value_offset, value_offset);
                }
                else {
                    stat = g_strdup_printf ("?item %s ?var%d . ?item %s ?var%d . FILTER ( !( ?var%d = ?var%d ) )",
                                            property_handler_get_name (meta_ref->metadata), value_offset,
                                            meta_ref->get_from_self, value_offset + 1,
                                            value_offset, value_offset + 1);
                    value_offset += 2;
                }
            }
        }
        else if (meta_ref->get_from_parent != NULL) {
            parent_type = item_handler_get_format (parent);
            while (parent_type != ITEM_IS_VIRTUAL_ITEM && parent_type != ITEM_IS_VIRTUAL_FOLDER) {
                parent = item_handler_get_parent (parent);
                parent_type = item_handler_get_format (parent);
            }

            if (parent != NULL) {
                if (meta_ref->operator == METADATA_OPERATOR_IS_EQUAL) {
                    if (meta_ref->means_subject == TRUE) {
                        stat = g_strdup_printf ("?item %s \"%s\"",
                                                property_handler_get_name (meta_ref->metadata), item_handler_get_subject (parent));
                    }
                    else {
                        stat = g_strdup_printf ("?item %s ?var%d . <%s> %s ?var%d",
                                                property_handler_get_name (meta_ref->metadata), value_offset,
                                                item_handler_get_subject (parent), meta_ref->get_from_parent, value_offset);
                        value_offset++;
                    }
                }
                else {
                    if (meta_ref->means_subject == TRUE) {
                        stat = g_strdup_printf ("?item %s ?var%d . FILTER ( !( ?var%d = \"%s\" ) )",
                                                property_handler_get_name (meta_ref->metadata), value_offset,
                                                value_offset, item_handler_get_subject (parent));
                    }
                    else {
                        stat = g_strdup_printf ("?item %s ?var%d . <%s> %s ?var%d . FILTER ( !( ?var%d = ?var%d ) )",
                                                property_handler_get_name (meta_ref->metadata), value_offset,
                                                item_handler_get_subject (parent), meta_ref->get_from_parent, value_offset + 1,
                                                value_offset, value_offset + 1);
                        value_offset += 2;
                    }
                }
            }
            else {
                g_warning ("Required a parent node, but none supplied");
            }
        }

        if (stat != NULL)
            statements = g_list_prepend (statements, stat);
    }

    *offset = value_offset;
    return g_list_reverse (statements);
}

static gchar* build_sparql_query (gchar to_get, GList *statements)
{
    gchar get_iter;
    gchar *stats;
    GString *query;

    query = g_string_new ("SELECT ?item ");

    for (get_iter = 'a'; get_iter < to_get; get_iter++)
        g_string_append_printf (query, "?%c ", get_iter);

    stats = from_glist_to_string (statements, " . ", TRUE);
    g_string_append_printf (query, "WHERE { %s }", stats);
    g_free (stats);

    return g_string_free (query, FALSE);
}

static GList* build_items (HierarchyNode *node, ItemHandler *parent, GPtrArray *data, GList *required)
{
    register int i;
    gchar **values;
    GList *required_iter;
    GList *items;
    ItemHandler *item;

    items = NULL;

    for (i = 0; i < data->len; i++) {
        values = (gchar**) g_ptr_array_index (data, i);
        item = g_object_new (ITEM_HANDLER_TYPE, "type", node->priv->type, "parent", parent, "node", node, "subject", *values, NULL);
        values++;

        for (required_iter = required; required_iter && *values != NULL; required_iter = g_list_next (required_iter), values++) {
            item_handler_load_metadata (item, (gchar*) required_iter->data, *values);
            values++;
        }

        items = g_list_prepend (items, item);
    }

    return g_list_reverse (items);
}

static void create_fetching_query_statement (const gchar *metadata, GList **statements, GList **required, gchar *var)
{
    gchar *statement;

    statement = g_strdup_printf ("?item %s ?%c", metadata, *var);
    *statements = g_list_prepend (*statements, statement);
    *required = g_list_prepend (*required, (gchar*) metadata);
    (*var)++;
}

static GList* collect_children_from_storage (HierarchyNode *node, ItemHandler *parent)
{
    int values_offset;
    gchar var;
    gchar *sparql;
    GList *iter;
    GList *statements;
    GList *more_statements;
    GList *items;
    GList *required;
    GPtrArray *response;
    GError *error;
    PropertyHandler *prop;
    ValuedMetadataReference *meta_ref;
    HierarchyNode *parent_node;

    var = 'a';
    statements = NULL;
    required = NULL;

    for (iter = node->priv->expose_policy.exposed_metadata; iter; iter = g_list_next (iter)) {
        prop = (PropertyHandler*) iter->data;
        create_fetching_query_statement (property_handler_get_name (prop), &statements, &required, &var);
    }

    for (iter = node->priv->expose_policy.conditional_metadata; iter; iter = g_list_next (iter)) {
        meta_ref = (ValuedMetadataReference*) iter->data;
        create_fetching_query_statement (property_handler_get_name (meta_ref->metadata), &statements, &required, &var);
    }

    values_offset = 0;

    more_statements = condition_policy_to_sparql (&(node->priv->self_policy), parent, &values_offset);
    statements = g_list_concat (statements, more_statements);

    parent_node = node->priv->node;

    while (parent_node != NULL) {
        more_statements = condition_policy_to_sparql (&(parent_node->priv->child_policy), parent, &values_offset);
        statements = g_list_concat (statements, more_statements);

        if (parent_node->priv->child_policy.inherit == TRUE)
            parent_node = parent_node->priv->node;
        else
            break;
    }

    sparql = build_sparql_query (var, statements);
    error = NULL;

    response = tracker_resources_sparql_query (get_tracker_client (), sparql, &error);
    if (response == NULL) {
        g_warning ("Unable to fetch items: %s", error->message);
        g_error_free (error);
        return NULL;
    }

    required = g_list_reverse (required);
    items = build_items (node, parent, response, required);

    g_ptr_array_foreach (response, (GFunc) g_strfreev, NULL);
    g_ptr_array_free (response, TRUE);
    g_list_free (required);
    g_free (sparql);

    return items;
}

static GList* collect_children_from_filesystem (HierarchyNode *node, ItemHandler *parent)
{
    int n;
    register int i;
    gchar *path;
    gchar *item_path;
    GList *ret;
    struct dirent **namelist;
    struct stat sbuf;
    ItemHandler *witem;
    NodesCache *cache;
    CONTENT_TYPE type;

    path = NULL;
    ret = NULL;

    if (parent != NULL) {
        if (item_handler_get_format (parent) == ITEM_IS_SHADOW_FOLDER)
            path = g_strdup (item_handler_real_path (parent));
    }

    if (path == NULL)
        path = g_strdup (node->priv->mapped_folder);

    cache = get_cache_reference ();

    check_and_create_folder (path);
    n = scandir (path, &namelist, 0, alphasort);

    for (i = 2; i < n; i++) {
        item_path = g_build_filename (path, namelist [i]->d_name, NULL);
        witem = nodes_cache_get_by_path (cache, item_path);

        if (witem != NULL) {
            g_free (item_path);
        }
        else {
            stat (item_path, &sbuf);
            if (S_ISDIR (sbuf.st_mode))
                type = ITEM_IS_SHADOW_FOLDER;
            else
                type = ITEM_IS_SHADOW_ITEM;

            witem = g_object_new (ITEM_HANDLER_TYPE,
                                "type", type,
                                "parent", parent,
                                "node", node,
                                "file_path", item_path,
                                "exposed_name", namelist [i]->d_name, NULL);

            nodes_cache_set_by_path (cache, witem, item_path);
        }

        ret = g_list_prepend (ret, witem);
        free (namelist [i]);
    }

    free (namelist [0]);
    free (namelist [1]);
    free (namelist);
    g_free (path);
    return g_list_reverse (ret);
}

static GList* collect_children_static (HierarchyNode *node, ItemHandler *parent)
{
    ItemHandler *witem;

    witem = g_object_new (ITEM_HANDLER_TYPE,
                          "type", ITEM_IS_STATIC_FOLDER,
                          "parent", parent,
                          "node", node,
                          "file_path", getenv ("HOME"),
                          "exposed_name", node->priv->expose_policy.formula, NULL);

    return g_list_prepend (NULL, witem);
}

/**
 * hierarchy_node_get_children:
 * @node: a #HierarchyNode
 * @parent: item to use as pivot for the search and if specified the function
 * returns only #ItemHandler which are child of this, or NULL
 *
 * Retrieves contents for the specified #HierarchyNode
 *
 * Return value: a list of #ItemHandler
 **/
GList* hierarchy_node_get_children (HierarchyNode *node, ItemHandler *parent)
{
    GList *ret;

    if (node->priv->type == ITEM_IS_SHADOW_FOLDER)
        ret = collect_children_from_filesystem (node, parent);
    else if (node->priv->type == ITEM_IS_STATIC_FOLDER)
        ret = collect_children_static (node, parent);
    else
        ret = collect_children_from_storage (node, parent);

    return ret;
}

/**
 * hierarchy_node_get_subchildren:
 * @node: a #HierarchyNode
 * @parent: item to use as pivot for the search and if specified the function
 * returns only #ItemHandler which are child of this, or NULL
 *
 * Retrieves contents from #HierarchyNode s under the specified @node
 *
 * Return value: a list of #ItemHandler, must be freed with g_list_free()
 * when no longer in use
 **/
GList* hierarchy_node_get_subchildren (HierarchyNode *node, ItemHandler *parent)
{
    GList *nodes;
    GList *ret;
    GList *subchildren;
    HierarchyNode *child;

    ret = NULL;

    if (hierarchy_node_get_format (node) == ITEM_IS_SHADOW_FOLDER &&
            parent != NULL && item_handler_get_format (parent) == ITEM_IS_SHADOW_FOLDER) {
        ret = hierarchy_node_get_children (node, parent);
    }
    else {
        for (nodes = node->priv->children; nodes; nodes = g_list_next (nodes)) {
            child = (HierarchyNode*) nodes->data;

            subchildren = hierarchy_node_get_children (child, parent);
            if (subchildren == NULL)
                continue;

            if (ret == NULL)
                ret = subchildren;
            else
                ret = g_list_concat (ret, subchildren);
        }
    }

    return ret;
}

static void retrieve_metadata_by_name (HierarchyNode *node, ItemHandler *item, const gchar *name)
{
    int num;
    int i;
    int valid_extracted_senteces;
    int compilation;
    gchar *str;
    gboolean metadata_set;
    GList *iter;
    GList *metadata_offset;
    regex_t reg;
    regmatch_t *matches;
    ValuedMetadataReference *metadata_ref;
    SaveExtractName *policy;

    policy = &(node->priv->save_policy.extraction_behaviour);

    if (policy->formula == NULL) {
        g_warning ("Error: missing exposing formula in %s", node->priv->name);
        return;
    }

    num = g_list_length (policy->assigned_metadata) + 1;
    matches = alloca (sizeof (regmatch_t) * num);
    memset (matches, 0, sizeof (regmatch_t) * num);

    compilation = regcomp (&reg, policy->formula, REG_EXTENDED);

    if (compilation != 0) {
        g_warning ("Unable to compile matching regular expression: %d", compilation);
    }
    else {
        if (regexec (&reg, name, num, matches, 0) == REG_NOMATCH) {
            g_warning ("Unable to match expression while parsing new file's name");
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

                for (iter = metadata_offset; iter; iter = g_list_next (iter)) {
                    metadata_ref = (ValuedMetadataReference*) iter->data;

                    if (metadata_ref->get_from_extraction == i) {
                        item_handler_load_metadata (item, property_handler_get_name (metadata_ref->metadata), str);
                        metadata_set = TRUE;

                        /*
                            This truly depends on sorting acted in parse_saving_policy()
                        */
                        metadata_offset = g_list_next (iter);
                        break;
                    }
                }

                if (metadata_set == FALSE)
                    g_free (str);
            }
        }
    }

    for (iter = policy->assigned_metadata; iter; iter = g_list_next (iter)) {
        metadata_ref = (ValuedMetadataReference*) iter->data;

        if (metadata_ref->fixed_value != NULL) {
            if (metadata_ref->condition_from_extraction != 0) {
                i = metadata_ref->condition_from_extraction;
                if (i > valid_extracted_senteces || (matches [i].rm_so == matches [i].rm_eo))
                    continue;
            }

            item_handler_load_metadata (item, property_handler_get_name (metadata_ref->metadata), metadata_ref->fixed_value);
        }

        /**
            TODO    Has this to be completed with exotic metadata assignments handling?
        */
    }
}

static void inherit_metadata (HierarchyNode *node, ItemHandler *item, ItemHandler *parent)
{
    const gchar *val;
    GList *iter;
    ValuedMetadataReference *metadata;

    if (node == NULL) {
        g_warning ("Error: logic node is not set");
        return;
    }

    for (iter = node->priv->save_policy.inheritable_assignments; iter; iter = g_list_next (iter)) {
        metadata = (ValuedMetadataReference*) iter->data;

        if (metadata->fixed_value != NULL) {
            item_handler_load_metadata (item, property_handler_get_name (metadata->metadata), metadata->fixed_value);
        }
        else if (metadata->get_from_self != NULL) {
            if (metadata->means_subject == TRUE)
                val = item_handler_get_subject (item);
            else
                val = item_handler_get_metadata (item, metadata->get_from_self);

            if (val != NULL)
                item_handler_load_metadata (item, property_handler_get_name (metadata->metadata), (gchar*) val);
        }
        else if (metadata->get_from_parent != NULL) {
            if (parent == NULL) {
                g_warning ("Error: required a metadata from parent, but parent node is not set");
            }
            else {
                if (metadata->means_subject == TRUE)
                    val = item_handler_get_subject (parent);
                else
                    val = item_handler_get_metadata (parent, metadata->get_from_parent);

                if (val != NULL)
                    item_handler_load_metadata (item, property_handler_get_name (metadata->metadata), (gchar*) val);
            }
        }
    }

    if (node->priv->save_policy.inherit == TRUE && node->priv->node != NULL)
        inherit_metadata (node->priv->node, item, item_handler_get_parent (parent));
}

static void assign_path (ItemHandler *item)
{
    int fd;
    gchar *path;
    gchar *tmp;
    GList *tokens;
    GList *iter;
    HierarchyNode *node;
    ItemHandler *parent;

    node = item_handler_get_logic_node (item);

    if (node->priv->save_policy.hijack_folder == NULL) {
        /**
            TODO    The effective saving-tree has to be used to retrieve the real path of the new
                    item, this portion has to be removed as soon as possible
        */
        path = g_build_filename (getenv ("HOME"), ".avfs_saving", NULL);
        check_and_create_folder (path);
        g_free (path);

        path = g_build_filename (getenv ("HOME"), ".avfs_saving", "XXXXXX", NULL);

        if (item_handler_is_folder (item)) {
            mkdtemp (path);
        }
        else {
            fd = mkstemp (path);
            close (fd);
        }

        tmp = g_filename_to_uri (path, NULL, NULL);
        g_object_set (item, "subject", tmp, NULL);
        g_free (tmp);
    }
    else {
        /**
            TODO    This can be greatly improved...
        */
        tokens = NULL;
        parent = item_handler_get_parent (item);

        while (item_handler_get_logic_node (parent) == node) {
            tokens = g_list_prepend (tokens, (gchar*) item_handler_exposed_name (parent));
            parent = item_handler_get_parent (parent);
        }

        path = g_strdup (node->priv->save_policy.hijack_folder);

        if (tokens != NULL) {
            tokens = g_list_reverse (tokens);

            for (iter = tokens; iter; iter = g_list_next (iter)) {
                tmp = g_build_filename (path, (gchar*) tokens->data, NULL);
                g_free (path);
                path = tmp;
            }

            g_list_free (tokens);
        }

        tmp = g_build_filename (path, item_handler_exposed_name (item), NULL);
        g_free (path);
        path = tmp;

        if (item_handler_is_folder (item))
            mkdir (path, 0770);
        else
            fclose (fopen (path, "w"));
    }

    g_object_set (item, "file_path", path, NULL);
    g_free (path);
}

/**
 * hierarchy_node_add_item:
 * @node: a #HierarchyNode
 * @type: type of the new item
 * @parent: parent #ItemHandler for the new item
 * @newname: name of the new item
 *
 * To add an #ItemHandler to the specified #node. Accordly to the configuration this new element
 * will be assigned to a file in the real filesystem and will be eventually assigned a set of
 * metadata
 *
 * Return value: the newly created #ItemHandler
 **/
ItemHandler* hierarchy_node_add_item (HierarchyNode *node, NODE_TYPE type, ItemHandler *parent, const gchar *newname)
{
    ItemHandler *new_item;

    if (node->priv->save_policy.writable == FALSE) {
        g_warning ("Hierarchy node %s is not writable", node->priv->name);
        return NULL;
    }

    /*
        Remember: not only ITEM_IS_SHADOW_FOLDERs create their contents on the specific path, but
        every node with the new-shadow-content tag in their saving-policy
    */
    if (node->priv->save_policy.hijack_folder == NULL) {
        new_item = item_handler_new_alloc (type == NODE_IS_FOLDER ? ITEM_IS_VIRTUAL_FOLDER : ITEM_IS_VIRTUAL_ITEM, node, parent);
        g_object_set (new_item, "exposed_name", newname, NULL);

        retrieve_metadata_by_name (node, new_item, newname);
        inherit_metadata (node, new_item, parent);
        assign_path (new_item);
        item_handler_flush (new_item);
    }
    else {
        new_item = item_handler_new_alloc (type == NODE_IS_FOLDER ? ITEM_IS_SHADOW_FOLDER : ITEM_IS_SHADOW_ITEM, node, parent);
        g_object_set (new_item, "exposed_name", newname, NULL);

        assign_path (new_item);
    }

    return new_item;
}

/**
 * hierarchy_node_exposed_name_for_item:
 * @node: a #HierarchyNode
 * @item: a #ItemHandler
 *
 * Retrieves the exposed name for the specified @item in the @node, accordly his "expose policy"
 *
 * Return value: the name which rappresents @item on the filesystem
 **/
gchar* hierarchy_node_exposed_name_for_item (HierarchyNode *node, ItemHandler *item)
{
    register int i;
    int len;
    const gchar *val;
    GList *metadata_iter;
    GList *conditional_iter;
    GString *result;
    ExposePolicy *exp;
    PropertyHandler *prop;
    ValuedMetadataReference *conditional;

    exp = &(node->priv->expose_policy);

    if (exp->formula == NULL) {
        g_warning ("Runtime error: exposing formula not available in node");
        return NULL;
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
                g_warning ("Runtime error: required a metadata not defined in configuration");
            }
            else {
                prop = (PropertyHandler*) metadata_iter->data;
                val = item_handler_get_metadata (item, property_handler_get_name (prop));

                if (val != NULL)
                    g_string_append_printf (result, "%s", val);

                metadata_iter = g_list_next (metadata_iter);
            }

            i++;
        }
        else if (exp->formula [i] == '\\' && g_ascii_isdigit (exp->formula [i + 1])) {
            conditional = (ValuedMetadataReference*) conditional_iter->data;
            val = item_handler_get_metadata (item, property_handler_get_name (conditional->metadata));

            if (val != NULL && strcmp (val, conditional->comparison_value) == 0)
                g_string_append_printf (result, "%s", conditional->fixed_value);

            conditional_iter = g_list_next (conditional_iter);
            i++;
        }
        else {
            g_string_append_printf (result, "%c", exp->formula [i]);
        }
    }

    return g_string_free (result, FALSE);
}
