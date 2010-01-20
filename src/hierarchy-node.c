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

#include "hierarchy-node.h"
#include "property-handler.h"
#include "contents-plugin.h"
#include "hierarchy.h"
#include "nodes-cache.h"
#include "gfuse-loop.h"
#include "utils.h"
#include <wordexp.h>

#define HIERARCHY_NODE_GET_PRIVATE(obj)     (G_TYPE_INSTANCE_GET_PRIVATE ((obj), HIERARCHY_NODE_TYPE, HierarchyNodePrivate))

typedef struct _ExposePolicy            ExposePolicy;
typedef int (*ContentCallback)          (ExposePolicy *policy, ItemHandler *item, int flags);

gchar       *SavingPath                 = NULL;

typedef enum {
    METADATA_OPERATOR_IS_EQUAL,
    METADATA_OPERATOR_IS_NOT_EQUAL,
} METADATA_OPERATOR;

typedef enum {
    METADATA_HOLDER_SELF,
    METADATA_HOLDER_PARENT,
} METADATA_HOLDER;

typedef struct {
    METADATA_HOLDER     from;
    TrackerProperty     *metadata;
    gboolean            means_subject;
} MetadataDesc;

typedef struct {
    TrackerProperty     *metadata;
    gchar               *formula;
    GList               *involved;                  // list of MetadataDesc
    gchar               *comparison_value;
    METADATA_OPERATOR   operator;
    int                 get_from_extraction;
    int                 condition_from_extraction;
} ValuedMetadataReference;

struct _ExposePolicy {
    gchar               *formula;
    GList               *exposed_metadata;          // list of MetadataDesc
    GList               *conditional_metadata;      // list of ValuedMetadataReference
    ContentsPlugin      *contents_callback;
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
} EditPolicy;

struct _HierarchyNodePrivate {
    CONTENT_TYPE        type;
    gchar               *name;
    HierarchyNode       *node;

    gchar               *additional_option;
    gboolean            hide_contents;
    EditPolicy          save_policy;
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
} HierarchyDescription [] = {
    { "root",             ITEM_IS_ROOT },
    { "file",             ITEM_IS_VIRTUAL_ITEM },
    { "folder",           ITEM_IS_VIRTUAL_FOLDER },
    { "static_folder",    ITEM_IS_STATIC_FOLDER },
    { "set_folder",       ITEM_IS_SET_FOLDER },
    { "mirror_content",   ITEM_IS_MIRROR_FOLDER },
    { "system_folders",   ITEM_IS_MIRROR_FOLDER },
    { NULL,               0 }
};

G_DEFINE_TYPE (HierarchyNode, hierarchy_node, G_TYPE_OBJECT);

static void free_metadata_reference (ValuedMetadataReference *ref)
{
    GList *iter;

    g_object_unref (ref->metadata);

    for (iter = ref->involved; iter; iter = g_list_next (iter))
        g_free (iter->data);

    g_free (ref);
}

static void free_expose_policy (ExposePolicy *policy)
{
    if (policy->formula != NULL)
        g_free (policy->formula);
}

static void free_condition_policy (ConditionPolicy *policy)
{
    GList *iter;

    for (iter = policy->conditions; iter; iter = g_list_next (iter))
        free_metadata_reference ((ValuedMetadataReference*) iter->data);
}

static void free_save_policy (EditPolicy *policy)
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

    if (node->priv->additional_option != NULL)
        g_free (node->priv->additional_option);

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

static GList* parse_reference_formula (gchar *value, gchar **output)
{
    int i;
    int e;
    int token_pos;
    int offset;
    int formula_size;
    gboolean handled;
    gchar *formula;
    gchar *meta_name;
    gchar *end_name;
    GList *ret;
    MetadataDesc *meta;

    i = 0;
    e = 0;
    token_pos = 1;
    meta = NULL;
    ret = NULL;
    formula_size = strlen (value) + 1;

    *output = g_new0 (gchar, formula_size);
    formula = *output;

    while (value [i] != '\0') {
        handled = FALSE;

        if (value [i] == '$') {
            if (meta == NULL)
                meta = g_new0 (MetadataDesc, 1);

            do {
                if (strncmp (value + i + 1, "parent", 6) == 0) {
                    meta->from = METADATA_HOLDER_PARENT;
                    offset = 6;
                }
                else if (strncmp (value + i + 1, "self", 4) == 0) {
                    meta->from = METADATA_HOLDER_SELF;
                    offset = 4;
                }
                else
                    break;

                meta_name = value + i + 1 + offset;

                if (*meta_name != '{')
                    break;

                meta_name++;

                end_name = strchr (meta_name, '}');
                if (end_name == NULL)
                    break;

                *end_name = '\0';

                if (strcmp (meta_name, "/subject") == 0) {
                    meta->means_subject = TRUE;
                }
                else {
                    meta->metadata = properties_pool_get_by_name (meta_name);
                    if (meta->metadata == NULL) {
                        g_warning ("Unable to retrieve metadata claimed in rule: %s\n", meta_name);
                        break;
                    }
                }

                ret = g_list_prepend (ret, meta);
                meta = NULL;

                e += snprintf (formula + e, formula_size - e, "\\%d", token_pos);
                token_pos++;

                handled = TRUE;
                i += 1 + offset + strlen (meta_name) + 2;

            } while (0);
        }

        if (handled == FALSE) {
            formula [e] = value [i];
            e++;
            i++;
        }
    }

    if (meta != NULL)
        g_free (meta);

    if (ret != NULL)
        return g_list_reverse (ret);
    else
        return NULL;
}

static ValuedMetadataReference* parse_reference_to_metadata (const gchar *tag, xmlNode *node)
{
    gchar *str;
    gchar *value;
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

    ref->metadata = properties_pool_get_by_name (str);
    xmlFree (str);

    if (ref->metadata == NULL)
        return NULL;

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

    value = (gchar*) xmlGetProp (node, (xmlChar*) "value");

    if (value == NULL) {
        value = (gchar*) xmlGetProp (node, (xmlChar*) "valuefromextract");

        if (value != NULL) {
            ref->get_from_extraction = strtoull (value, &str, 10);
            if (*str != '\0' || ref->get_from_extraction < 1)
                g_warning ("Error: using a non valid offset with 'valuefromextract' attribute: %s", value);
            xmlFree (value);
        }
        else {
            g_warning ("Error: unrecognized metadata assignment behaviour in %s", (gchar*) node->name);
            free_metadata_reference (ref);
            ref = NULL;
        }
    }
    else {
        ref->involved = parse_reference_formula (value, &(ref->formula));
        xmlFree (value);
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

static gboolean parse_editing_policy (EditPolicy *saving, xmlNode *root)
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
                    if (ref->get_from_extraction != 0 && saving->extraction_behaviour.formula == NULL)
                        g_warning ("Defined a positional extraction policy for new files, but missing extraction formula.");

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
        else if (strcmp ((gchar*) node->name, "new_mirror_content") == 0) {
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

static gboolean parse_content_policy (HierarchyNode *parent, xmlNode *root)
{
    gboolean ret;
    GList *children;
    xmlNode *node;
    HierarchyNode *child;

    children = NULL;
    ret = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        child = hierarchy_node_new_from_xml (parent, node);
        if (child != NULL)
            children = g_list_prepend (children, child);
    }

    if (children != NULL)
        parent->priv->children = g_list_reverse (children);

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

static gboolean parse_exposing_policy (HierarchyNode *this, ExposePolicy *exposing, xmlNode *root)
{
    gchar *str;
    gboolean ret;
    xmlNode *node;
    xmlNode *subnode;

    ret = TRUE;

    this->priv->self_policy.inherit = TRUE;
    this->priv->child_policy.inherit = TRUE;

    for (node = root->children; ret == TRUE && node; node = node->next) {
        if (strcmp ((gchar*) node->name, "name") == 0) {
            str = (gchar*) xmlGetProp (node, (xmlChar*) "value");
            if (str != NULL) {
                exposing->exposed_metadata = parse_reference_formula (str, &(exposing->formula));
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
            if (hierarchy_node_get_format (this) == ITEM_IS_VIRTUAL_ITEM) {
                subnode = node->children;

                if (subnode != NULL) {
                    /**
                        TODO    Better contents plugin initialization
                    */

                    str = (gchar*) subnode->name;
                    exposing->contents_callback = retrieve_contents_plugin (str);

                    if (exposing->contents_callback == NULL) {
                        g_warning ("Unable to identify contents exposing policy, found %s", str);
                        ret = FALSE;
                    }
                }
            }
            else {
                ret = parse_content_policy (this, node);
            }
        }
        else if (strcmp ((gchar*) node->name, "self_conditions") == 0) {
            ret = parse_conditions_policy (&(this->priv->self_policy), node);
        }
        else if (strcmp ((gchar*) node->name, "inheritable_conditions") == 0) {
            ret = parse_conditions_policy (&(this->priv->child_policy), node);
        }
        else if (strcmp ((gchar*) node->name, "comment") == 0) {
            continue;
        }
        else {
            g_warning ("Unrecognized tag %s", (gchar*) node->name);
        }
    }

    return ret;
}

static void add_base_path (HierarchyNode *this, gchar *path, xmlNode *root)
{
    gchar *str;

    if (path == NULL) {
        str = (gchar*) xmlGetProp (root, (xmlChar*) "base_path");
        if (str != NULL) {
            this->priv->additional_option = expand_path_to_absolute (str);
            free (str);
        }
        else {
            g_warning ("Undefined base path for mirror node");
        }
    }
    else {
        this->priv->additional_option = expand_path_to_absolute (path);
    }

    this->priv->save_policy.hijack_folder = g_strdup (this->priv->additional_option);
    this->priv->save_policy.writable = TRUE;
}

static void add_grouping_metadata (HierarchyNode *this, xmlNode *root)
{
    gchar *str;

    str = (gchar*) xmlGetProp (root, (xmlChar*) "metadata");
    if (str != NULL)
        this->priv->additional_option = str;
}

static void add_hide_property (HierarchyNode *this, xmlNode *root)
{
    gchar *str;

    str = (gchar*) xmlGetProp (root, (xmlChar*) "hidden");
    if (str != NULL) {
        if (strcmp (str, "yes") == 0)
            this->priv->hide_contents = TRUE;
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

            /**
                TODO    If a "system_folders" tag is found, test for the CAP_SYS_ADMIN capability
                        and automatically bind /proc, /dev and /tmp as internal mountpoint
            */
            if (HierarchyDescription [i].type == ITEM_IS_MIRROR_FOLDER) {
                /*
                    <system_folders> are managed just as <mirror_content>,
                    but as predefined base_path uses "/"
                */
                if (strcmp (HierarchyDescription [i].tag, "system_folders") == 0)
                    add_base_path (this, "/", NULL);
                else
                    add_base_path (this, NULL, root);
            }
            else if (HierarchyDescription [i].type == ITEM_IS_SET_FOLDER) {
                add_grouping_metadata (this, root);
            }

            add_hide_property (this, root);
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
            if (strcmp ((gchar*) node->name, "editing_policy") == 0) {
                ret = parse_editing_policy (&(this->priv->save_policy), node);
            }
            else if (strcmp ((gchar*) node->name, "visualization_policy") == 0) {
                ret = parse_exposing_policy (this, &(this->priv->expose_policy), node);
            }
            else {
                g_warning ("Unrecognized tag %s in %s", (gchar*) node->name, this->priv->name);
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

static gchar* compose_value_from_many_metadata (GList *metadata, gchar *formula)
{
    /**
        TODO    Here we have to support concatenation of different objects to match a single one.
                The main problem is to write a SPARQL query able to apply concatenation in place
    */

    g_warning ("Metadata concatenation not yet supported");
    return NULL;
}

static GList* condition_policy_to_sparql (ConditionPolicy *policy, ItemHandler *parent, int *offset)
{
    int value_offset;
    int involved_num;
    gchar *stat;
    gchar *val;
    gchar *true_val;
    const gchar *meta_name;
    GList *iter;
    GList *statements;
    TrackerClass *class;
    ValuedMetadataReference *meta_ref;
    MetadataDesc *component;

    statements = NULL;
    value_offset = *offset;

    for (iter = policy->conditions; iter; iter = g_list_next (iter)) {
        meta_ref = (ValuedMetadataReference*) iter->data;
        stat = NULL;

        involved_num = g_list_length (meta_ref->involved);

        /*
            Here we try to optimize conditions in which the metadata has to match a specific
            value or a specific other metadata, embedding that condition in the main SPARQL query
        */

        if (involved_num == 1 && strcmp (meta_ref->formula, "\\1") == 0) {
            component = (MetadataDesc*) meta_ref->involved->data;

            if (component->from == METADATA_HOLDER_SELF) {
                if (meta_ref->operator == METADATA_OPERATOR_IS_EQUAL) {
                    if (component->means_subject == TRUE) {
                        stat = g_strdup_printf ("?item %s ?item", tracker_property_get_name (meta_ref->metadata));
                    }
                    else {
                        stat = g_strdup_printf ("?item %s ?var%d . ?item %s ?var%d",
                                                tracker_property_get_name (meta_ref->metadata), value_offset,
                                                tracker_property_get_name (component->metadata), value_offset);
                        value_offset++;
                    }
                }
                else {
                    if (component->means_subject == TRUE) {
                        stat = g_strdup_printf ("?item %s ?var%d . FILTER ( !( ?var%d = ?item ) )",
                                                tracker_property_get_name (meta_ref->metadata), value_offset, value_offset);
                    }
                    else {
                        stat = g_strdup_printf ("?item %s ?var%d . ?item %s ?var%d . FILTER ( !( ?var%d = ?var%d ) )",
                                                tracker_property_get_name (meta_ref->metadata), value_offset,
                                                tracker_property_get_name (component->metadata), value_offset + 1,
                                                value_offset, value_offset + 1);
                        value_offset += 2;
                    }
                }
            }
            else if (component->from == METADATA_HOLDER_PARENT) {
                while (parent != NULL && item_handler_type_has_metadata (parent) == FALSE)
                    parent = item_handler_get_parent (parent);

                if (parent != NULL) {
                    if (meta_ref->operator == METADATA_OPERATOR_IS_EQUAL) {
                        if (component->means_subject == TRUE) {
                            stat = g_strdup_printf ("?item %s \"%s\"",
                                                    tracker_property_get_name (meta_ref->metadata), item_handler_get_subject (parent));
                        }
                        else {
                            meta_name = tracker_property_get_name (component->metadata);

                            if (item_handler_contains_metadata (parent, meta_name)) {
                                stat = g_strdup_printf ("?item %s \"%s\"",
                                                        tracker_property_get_name (meta_ref->metadata),
                                                        item_handler_get_metadata (parent, meta_name));
                            }
                            else {
                                stat = g_strdup_printf ("?item %s ?var%d . <%s> %s ?var%d",
                                                        tracker_property_get_name (meta_ref->metadata), value_offset,
                                                        item_handler_get_subject (parent), meta_name, value_offset);
                                value_offset++;
                            }
                        }
                    }
                    else {
                        if (component->means_subject == TRUE) {
                            stat = g_strdup_printf ("?item %s ?var%d . FILTER ( !( ?var%d = \"%s\" ) )",
                                                    tracker_property_get_name (meta_ref->metadata), value_offset,
                                                    value_offset, item_handler_get_subject (parent));
                        }
                        else {
                            meta_name = tracker_property_get_name (component->metadata);

                            if (item_handler_contains_metadata (parent, meta_name)) {
                                stat = g_strdup_printf ("?item %s ?var%d . FILTER ( !( ?var%d = \"%s\" ) )",
                                                        tracker_property_get_name (meta_ref->metadata), value_offset,
                                                        value_offset, item_handler_get_metadata (parent, meta_name));
                                value_offset++;
                            }
                            else {
                                stat = g_strdup_printf ("?item %s ?var%d . <%s> %s ?var%d . FILTER ( !( ?var%d = ?var%d ) )",
                                                        tracker_property_get_name (meta_ref->metadata), value_offset,
                                                        item_handler_get_subject (parent), meta_name, value_offset + 1,
                                                        value_offset, value_offset + 1);
                                value_offset += 2;
                            }
                        }
                    }
                }
                else {
                    g_warning ("Required a parent node, but none supplied");
                }
            }
        }
        else {
            if (involved_num == 0)
                val = g_strdup (meta_ref->formula);
            else
                val = compose_value_from_many_metadata (meta_ref->involved, meta_ref->formula);

            if (val != NULL) {
                switch (tracker_property_get_data_type (meta_ref->metadata)) {
                    case TRACKER_PROPERTY_TYPE_STRING:
                        true_val = g_strdup_printf ("\"%s\"", val);
                        g_free (val);
                        val = true_val;
                        break;

                    /**
                        TODO    This is probably incorrect, or may be better managed. We need to
                                know if the value is to wrap between quotes (perhaps because it
                                involves a subject) or not (because is a class), suggestions are
                                welcome
                    */
                    case TRACKER_PROPERTY_TYPE_RESOURCE:
                        class = tracker_property_get_range (meta_ref->metadata);
                        if (strcmp (tracker_class_get_name (class), "rdfs:Class") != 0) {
                            true_val = g_strdup_printf ("\"%s\"", val);
                            g_free (val);
                            val = true_val;
                        }
                        break;

                    default:
                        break;
                }

                if (meta_ref->operator == METADATA_OPERATOR_IS_EQUAL) {
                    stat = g_strdup_printf ("?item %s %s", tracker_property_get_name (meta_ref->metadata), val);
                }
                else {
                    stat = g_strdup_printf ("?item %s ?var%d . FILTER ( !( ?var%d = %s ) )",
                                            tracker_property_get_name (meta_ref->metadata), value_offset, value_offset, val);
                    value_offset++;
                }

                g_free (val);
            }
        }

        if (stat != NULL)
            statements = g_list_prepend (statements, stat);
    }

    *offset = value_offset;
    return g_list_reverse (statements);
}

static gchar* build_sparql_query (gchar *selection, gchar to_get, GList *statements)
{
    gchar get_iter;
    gchar *stats;
    GString *query;

    if (selection == NULL) {
        query = g_string_new ("SELECT ?item ");
    }
    else {
        query = g_string_new (selection);
        g_string_append_printf (query, " ");
    }

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
    register int a;
    gchar **values;
    GList *required_iter;
    GList *items;
    ItemHandler *item;

    items = NULL;

    for (i = 0; i < data->len; i++) {
        values = (gchar**) g_ptr_array_index (data, i);

        item = g_object_new (ITEM_HANDLER_TYPE, "type", node->priv->type, "parent", parent, "node", node, "subject", values [0], NULL);

        for (a = 1, required_iter = required; required_iter && *values != NULL; a++, required_iter = g_list_next (required_iter), values++)
            item_handler_load_metadata (item, (gchar*) required_iter->data, values [a]);

        if (node->priv->expose_policy.contents_callback != NULL)
            g_object_set (item, "contents_handler", node->priv->expose_policy.contents_callback, NULL);

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
    ValuedMetadataReference *meta_ref;
    MetadataDesc *prop;
    HierarchyNode *parent_node;

    var = 'a';
    statements = NULL;
    required = NULL;

    for (iter = node->priv->expose_policy.exposed_metadata; iter; iter = g_list_next (iter)) {
        prop = (MetadataDesc*) iter->data;
        if (prop->from == METADATA_HOLDER_SELF && prop->means_subject == FALSE)
            create_fetching_query_statement (tracker_property_get_name (prop->metadata), &statements, &required, &var);
    }

    for (iter = node->priv->expose_policy.conditional_metadata; iter; iter = g_list_next (iter)) {
        meta_ref = (ValuedMetadataReference*) iter->data;
        create_fetching_query_statement (tracker_property_get_name (meta_ref->metadata), &statements, &required, &var);
    }

    values_offset = 0;

    more_statements = condition_policy_to_sparql (&(node->priv->self_policy), parent, &values_offset);
    statements = g_list_concat (statements, more_statements);

    if (node->priv->child_policy.inherit == TRUE) {
        parent_node = node->priv->node;

        while (parent_node != NULL) {
            more_statements = condition_policy_to_sparql (&(parent_node->priv->child_policy), parent, &values_offset);
            statements = g_list_concat (statements, more_statements);

            if (parent_node->priv->child_policy.inherit == TRUE)
                parent_node = parent_node->priv->node;
            else
                break;
        }
    }

    sparql = build_sparql_query (NULL, var, statements);
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
    GFuseLoop *loop;

    path = NULL;

    if (parent != NULL) {
        if (item_handler_get_format (parent) == ITEM_IS_MIRROR_FOLDER)
            path = strdupa (item_handler_real_path (parent));
    }

    if (path == NULL)
        path = strdupa (node->priv->additional_option);

    ret = NULL;
    cache = get_cache_reference ();

    check_and_create_folder (path);
    n = scandir (path, &namelist, NULL, alphasort);
    loop = gfuse_loop_get_current ();

    for (i = 2; i < n; i++) {
        if (namelist [i]->d_name == NULL)
            continue;

        item_path = g_build_filename (path, namelist [i]->d_name, NULL);
        witem = nodes_cache_get_by_path (cache, item_path);

        if (witem != NULL) {
            g_free (item_path);
        }
        else {
            /**
                TODO    When FSter maps the real filesystem, it seems having some trouble
                        stat'ing his current mountpoint. It is unclear if this is a FSter's bug,
                        a FUSE's bug or a normal condition: waiting for further investigations,
                        here we skip all paths matching with the current instance's mountpoint
                        (retrieved on startup)
            */
            if (strcmp (item_path, gfuse_loop_get_mountpoint (loop)) == 0) {
                g_free (item_path);
                continue;
            }

            stat (item_path, &sbuf);
            if (S_ISDIR (sbuf.st_mode))
                type = ITEM_IS_MIRROR_FOLDER;
            else
                type = ITEM_IS_MIRROR_ITEM;

            witem = g_object_new (ITEM_HANDLER_TYPE,
                                  "type", type, "parent", parent,
                                  "node", node, "file_path", item_path,
                                  "exposed_name", namelist [i]->d_name, NULL);

            nodes_cache_set_by_path (cache, witem, item_path);
        }

        ret = g_list_prepend (ret, witem);
        free (namelist [i]);
    }

    free (namelist [0]);
    free (namelist [1]);
    free (namelist);

    if (ret != NULL)
        return g_list_reverse (ret);
    else
        return NULL;
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

static GList* collect_children_set (HierarchyNode *node, ItemHandler *parent)
{
    register int i;
    int values_offset;
    gchar **values;
    gchar *sparql;
    GList *items;
    GList *statements;
    GList *more_statements;
    GPtrArray *response;
    GError *error;
    ItemHandler *item;
    HierarchyNode *parent_node;

    values_offset = 1;
    statements = NULL;
    statements = g_list_append (statements, g_strdup_printf ("?item %s ?a", node->priv->additional_option));

    more_statements = condition_policy_to_sparql (&(node->priv->self_policy), parent, &values_offset);
    statements = g_list_concat (statements, more_statements);

    if (node->priv->child_policy.inherit == TRUE) {
        parent_node = node->priv->node;

        while (parent_node != NULL) {
            more_statements = condition_policy_to_sparql (&(parent_node->priv->child_policy), parent, &values_offset);
            statements = g_list_concat (statements, more_statements);

            if (parent_node->priv->child_policy.inherit == TRUE)
                parent_node = parent_node->priv->node;
            else
                break;
        }
    }

    sparql = build_sparql_query ("SELECT DISTINCT(?a)", 'a', statements);
    error = NULL;

    response = tracker_resources_sparql_query (get_tracker_client (), sparql, &error);
    if (response == NULL) {
        g_warning ("Unable to fetch items: %s", error->message);
        g_error_free (error);
        return NULL;
    }

    items = NULL;

    for (i = 0; i < response->len; i++) {
        values = (gchar**) g_ptr_array_index (response, i);
        item = g_object_new (ITEM_HANDLER_TYPE, "type", node->priv->type, "parent", parent, "node", node, NULL);
        item_handler_load_metadata (item, node->priv->additional_option, values [0]);
        items = g_list_prepend (items, item);
    }

    g_ptr_array_foreach (response, (GFunc) g_strfreev, NULL);
    g_ptr_array_free (response, TRUE);
    g_free (sparql);

    return g_list_reverse (items);
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

    if (node->priv->type == ITEM_IS_MIRROR_FOLDER)
        ret = collect_children_from_filesystem (node, parent);
    else if (node->priv->type == ITEM_IS_STATIC_FOLDER)
        ret = collect_children_static (node, parent);
    else if (node->priv->type == ITEM_IS_SET_FOLDER)
        ret = collect_children_set (node, parent);
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

    if (hierarchy_node_get_format (node) == ITEM_IS_MIRROR_FOLDER &&
            parent != NULL && item_handler_get_format (parent) == ITEM_IS_MIRROR_FOLDER) {
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

/**
 * hierarchy_node_hide_contents:
 * @node: a #HierarchyNode
 *
 * To know if contents of the specified #HierarchyNode have to be hide in the
 * toplevel filesystem presentation. In this case, items are not listed into
 * the folders but may be accessed when explicitely referenced
 *
 * Return value: TRUE if contents of the @node are required to be hidden,
 * FALSE otherwise
 **/
gboolean hierarchy_node_hide_contents (HierarchyNode *node)
{
    return node->priv->hide_contents;
}

static gchar* collect_from_metadata_desc_list (gchar *formula, GList *components, ItemHandler *item, ItemHandler *parent)
{
    int current_offset;
    register int i;
    const gchar *metadata_value;
    GList *components_iter;
    GString *val;
    ItemHandler *reference;
    MetadataDesc *component;

    g_assert (formula != NULL);

    current_offset = 1;
    components_iter = components;
    val = g_string_new ("");

    for (i = 0; formula [i] != '\0'; i++) {
        /*
            ASCII rulez
        */
        if (formula [i] == '\\' && formula [i + 1] - 0x30 == current_offset && components_iter != NULL) {
            component = (MetadataDesc*) components_iter->data;

            switch (component->from) {
                case METADATA_HOLDER_SELF:
                    reference = item;
                    break;

                case METADATA_HOLDER_PARENT:
                    reference = parent;
                    break;
            }

            if (component->means_subject == TRUE)
                metadata_value = item_handler_get_subject (reference);
            else
                metadata_value = item_handler_get_metadata (reference, tracker_property_get_name (component->metadata));

            g_string_append_printf (val, "%s", metadata_value);
            components_iter = g_list_next (components_iter);
            i++;
            current_offset++;
        }
        else {
            g_string_append_printf (val, "%c", formula [i]);
        }
    }

    return g_string_free (val, FALSE);
}

static void retrieve_metadata_by_name (HierarchyNode *node, ItemHandler *item, ItemHandler *parent, const gchar *name)
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
                        item_handler_load_metadata (item, tracker_property_get_name (metadata_ref->metadata), str);
                        metadata_set = TRUE;

                        /*
                            This truly depends on sorting acted in parse_editing_policy()
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

    /*
        Here are assigned metadata collected in the "<new_file>" section but
        which are not set by the extraction formula
    */
    for (iter = policy->assigned_metadata; iter; iter = g_list_next (iter)) {
        metadata_ref = (ValuedMetadataReference*) iter->data;

        if (metadata_ref->get_from_extraction != 0)
            continue;

        if (metadata_ref->condition_from_extraction != 0) {
            i = metadata_ref->condition_from_extraction;
            if (i > valid_extracted_senteces || (matches [i].rm_so == matches [i].rm_eo))
                continue;
        }

        str = collect_from_metadata_desc_list (metadata_ref->formula, metadata_ref->involved, item, parent);
        if (str != NULL) {
            item_handler_load_metadata (item, tracker_property_get_name (metadata_ref->metadata), str);
            g_free (str);
        }
    }
}

static void inherit_metadata (HierarchyNode *node, ItemHandler *item, ItemHandler *parent)
{
    gchar *val;
    GList *iter;
    ValuedMetadataReference *metadata;

    if (node == NULL) {
        g_warning ("Error: logic node is not set");
        return;
    }

    for (iter = node->priv->save_policy.inheritable_assignments; iter; iter = g_list_next (iter)) {
        metadata = (ValuedMetadataReference*) iter->data;

        val = collect_from_metadata_desc_list (metadata->formula, metadata->involved, item, parent);
        if (val != NULL) {
            item_handler_load_metadata (item, tracker_property_get_name (metadata->metadata), val);
            g_free (val);
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
        if (SavingPath == NULL) {
            g_warning ("Saving path is not set, unable to save the file");
            return;
        }

        /**
            TODO    The effective saving-tree has to be used to retrieve the real path of the new
                    item, this portion has to be removed as soon as possible
        */

        path = g_build_filename (SavingPath, "XXXXXX", NULL);

        if (item_handler_is_folder (item)) {
            mkdtemp (path);
        }
        else {
            fd = mkstemp (path);
            close (fd);
        }

        tmp = g_filename_to_uri (path, NULL, NULL);
        g_object_set (item, "subject", tmp, NULL);
        item_handler_load_metadata (item, "nie:isStoredAs", tmp);
        g_free (tmp);
    }
    else {
        /**
            TODO    This can be greatly improved...
        */
        tokens = NULL;
        parent = item_handler_get_parent (item);

        while (parent != NULL && item_handler_get_logic_node (parent) == node) {
            tokens = g_list_prepend (tokens, (gchar*) item_handler_exposed_name (parent));
            parent = item_handler_get_parent (parent);
        }

        path = g_strdup (node->priv->save_policy.hijack_folder);

        if (tokens != NULL) {
            for (iter = tokens; iter; iter = g_list_next (iter)) {
                tmp = g_build_filename (path, (gchar*) iter->data, NULL);
                g_free (path);
                path = tmp;
            }

            g_list_free (tokens);
        }

        tmp = g_build_filename (path, item_handler_exposed_name (item), NULL);
        g_free (path);
        path = tmp;

        if (item_handler_is_folder (item))
            check_and_create_folder (path);
        else
            create_file (path);
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
        Remember: not only ITEM_IS_MIRROR_FOLDERs create their contents on the specific path, but
        every node with the new_mirror_content tag in their saving-policy
    */
    if (node->priv->save_policy.hijack_folder == NULL) {
        new_item = item_handler_new_alloc (type == NODE_IS_FOLDER ? ITEM_IS_VIRTUAL_FOLDER : ITEM_IS_VIRTUAL_ITEM, node, parent);
        g_object_set (new_item, "exposed_name", newname, NULL);

        retrieve_metadata_by_name (node, new_item, parent, newname);
        inherit_metadata (node, new_item, parent);
        assign_path (new_item);
        item_handler_flush (new_item);
    }
    else {
        new_item = item_handler_new_alloc (type == NODE_IS_FOLDER ? ITEM_IS_MIRROR_FOLDER : ITEM_IS_MIRROR_ITEM, node, parent);
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
    ExposePolicy *exp;

    exp = &(node->priv->expose_policy);

    if (exp->formula == NULL) {
        g_warning ("Runtime error: exposing formula not available in node");
        return NULL;
    }

    return collect_from_metadata_desc_list (exp->formula, exp->exposed_metadata, item, item_handler_get_parent (item));
}

/*
    Warning: this is only a temporary function to remove when a complete
    saving tree management will be ready
*/
void hierarchy_node_set_save_path (gchar *path)
{
    wordexp_t results;

    if (path != NULL && strlen (path) == 0)
        g_warning ("Invalid saving path configured");

    if (SavingPath != NULL)
        g_free (SavingPath);

    if (path == NULL) {
        SavingPath = NULL;
    }
    else {
        wordexp (path, &results, 0);

        if (results.we_wordc == 1) {
            SavingPath = g_strdup (results.we_wordv [0]);
            check_and_create_folder (SavingPath);
        }
        else {
            g_warning ("Saving path seems to be not valid: %s", path);
        }

        wordfree (&results);
    }
}
