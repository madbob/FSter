/*  Copyright (C) 2009 Itsme S.r.L.
 *  Copyright (C) 2012 Roberto Guido <roberto.guido@linux.it>
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

#include "core.h"
#include "hierarchy.h"
#include "gfuse-loop.h"

/**
    TODO    Better path for configuration file, based on prefix and sysconfdir
*/
#define DEFAULT_CONFIG_FILE         "/etc/fster/fster.xml"

/*
    Pointers have different sizes on 32 and 64 bits architectures
*/

#if defined(__LP64__) || defined(_LP64)

/**
    Casts the optional data field in fuse_file_info to an OpenedItem
*/
#define FI_TO_OPENED_ITEM(__fi,__ptr) {     \
    __ptr = (OpenedItem*) __fi->fh;         \
}

/**
    Casts an OpenedItem pointer to fit the optional data field in fuse_file_info
*/
#define OPENED_ITEM_TO_FI(__ptr,__fi) {     \
    __fi->fh = (unsigned long) __ptr;       \
}

#else

/**
    Casts the optional data field in fuse_file_info to an OpenedItem
*/
#define FI_TO_OPENED_ITEM(__fi,__ptr) {     \
    __ptr = (OpenedItem*) (int) __fi->fh;   \
}

/**
    Casts an OpenedItem pointer to fit the optional data field in fuse_file_info
*/
#define OPENED_ITEM_TO_FI(__ptr,__fi) {     \
    __fi->fh = (uint64_t) (int) __ptr;      \
}

#endif

enum {
    KEY_HELP,
    KEY_CONFIGFILE,
    KEY_VERSION,
    KEY_USER_PARAMETER
};

static struct fuse_opt fster_opts [] = {
    FUSE_OPT_KEY ("-h",         KEY_HELP),
    FUSE_OPT_KEY ("--help",     KEY_HELP),
    FUSE_OPT_KEY ("-c ",        KEY_CONFIGFILE),
    FUSE_OPT_KEY ("-V",         KEY_VERSION),
    FUSE_OPT_KEY ("--version",  KEY_VERSION),
    FUSE_OPT_KEY ("-p ",        KEY_USER_PARAMETER),
    FUSE_OPT_END
};

/**
    Wrapper around Item, contains some informations about opening status of files on the
    filesystem
*/
typedef struct {
    ItemHandler         *item;              /**< Reference to the opened Item */
    int                 fd;                 /**< Opened file descriptor, as returned by open() against the real path of "item" */
} OpenedItem;

struct {
    gchar               *conf_file;
} Config;

/**
    Frees an OpenedItem

    @param item             The OpenedItem to be freed
*/
static inline void free_opened_item (OpenedItem *item)
{
    free (item);
}

/**
    Create the structure describing an opened file. This has be used also for non-Item files
    (regular objects)

    @param item             ItemHandler to be added to the list, or NULL if a regular object is
                            opened
    @param fd               File descriptor opened over the Item file

    @return                 Newly allocated OpenedItem, to free with free_opened_item()
*/
static inline OpenedItem* allocate_opened_item (ItemHandler *item, int fd)
{
    OpenedItem *newer;

    newer = calloc (1, sizeof (OpenedItem));
    if (newer == NULL)
        return NULL;

    newer->item = item;
    newer->fd = fd;
    return newer;
}

static void free_conf ()
{
    if (Config.conf_file != NULL)
        g_free (Config.conf_file);

    set_user_param (NULL, NULL);
}

static inline void set_permissions ()
{
    struct fuse_context *context;

    /**
        TODO    Here we try to set UID and GID of the thread without check correct capabilities:
                if CAP_SETUID and CAP_SETGID are set it work, otherwise invocations just fail
                with no rumors. Perhaps a check at startup can be desired...
    */

    context = fuse_get_context ();
    setuid (context->uid);
    setgid (context->gid);
}

/**
    Creates a new item in the hierarchy

    @param path             Path where the item must be created
    @param type             Type of the new item, essentially if it is a file or a folder
    @param target           Pointer which will be assigned to the newly allocated item

    @return                 0 if successful, otherwise a negative value describing the error
*/
static int create_item_by_path (const gchar *path, NODE_TYPE type, ItemHandler **target)
{
    int parent_type;
    gchar *name;
    ItemHandler *item;
    ItemHandler *parent;

    name = g_path_get_dirname (path);

    parent = verify_exposed_path (name);
    if (parent == NULL) {
        g_free (name);
        return -ENOTDIR;
    }

    g_free (name);
    name = g_path_get_basename (path);

    item = verify_exposed_path_in_folder (NULL, parent, name);
    if (item != NULL) {
        g_free (name);
        return -EEXIST;
    }

    parent_type = item_handler_get_format (parent);
    if (parent_type != ITEM_IS_VIRTUAL_FOLDER && parent_type != ITEM_IS_MIRROR_FOLDER &&
            parent_type != ITEM_IS_STATIC_FOLDER && parent_type != ITEM_IS_SET_FOLDER) {
        g_free (name);
        return -ENOTDIR;
    }

    item = item_handler_attach_child (parent, type, name);
    g_free (name);

    if (item == NULL)
        return -EACCES;

    if (target != NULL)
        *target = item;

    return 0;
}

/**
    Retrieve informations about a file

    @param path             Path of the file to check out
    @param stbuf            Struct to fill with informations

    @return                 0 if successful, otherwise a negative value describing the error
*/
static int ifs_getattr (const char *path, struct stat *stbuf)
{
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);
    return item_handler_stat (target, stbuf);
}

/**
    Check permissions for a file

    @param path             Path of the file for which check permissions
    @param mask             Permissions mask to check

    @return                 access()
*/
static int ifs_access (const char *path, int mask)
{
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);
    return item_handler_access (target, mask);
}

/**
    Read the contents of a symbolic link

    @param path             Path of the link to read
    @param buf              String filled with the path pointed by the link
    @param size             Size of the allocation of "buf"

    @return                 0 if successfull, or -EACCES if the directory is required outside
                            permitted hierarchy
*/
static int ifs_readlink (const char *path, char *buf, size_t size)
{
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);
    return item_handler_readlink (target, buf, size);
}

/**
    Retrieve contents for a folder

    @param path             Path in the filesystem to iterate
    @param buf              Buffer to fill, is used as parameter of "filler"
    @param filler           Callback to call for each found file
    @param offset           Starting offset for reading
    @param fi               Unused

    @return                 0 if successfull, or a negative value
*/
static int ifs_readdir (const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    int ret;
    const gchar *name;
    gchar *file_path;
    struct stat st;
    struct stat *ptr_st;
    GList *items;
    GList *iter;
    ItemHandler *target;
    ItemHandler *child;
    NodesCache *cache;

    set_permissions ();
    target = verify_exposed_path (path);

    if (target == NULL) {
        ret = -ENOENT;
    }
    else {
        if (item_handler_is_folder (target) == FALSE) {
            ret = -ENOTDIR;
        }
        else {
            cache = get_cache_reference ();
            items = item_handler_get_children (target);

            for (iter = items; iter; iter = g_list_next (iter)) {
                child = (ItemHandler*) iter->data;

                if (item_handler_get_hidden (child) == TRUE)
                    continue;

                name = item_handler_exposed_name (child);
                if (name == NULL)
                    continue;

                if (item_handler_stat (child, &st) == 0)
                    ptr_st = &st;
                else
                    ptr_st = NULL;

                file_path = g_build_filename (path, name, NULL);
                nodes_cache_set_by_path (cache, child, file_path);

                if (filler (buf, name, ptr_st, 0))
                    break;
            }

            g_list_free (items);
            ret = 0;
        }
    }

    return ret;
}

/**
    Callback to create special nodes. This is possible only in temporary paths

    @param path             Path of the new special node
    @param mode             Permissions for the new node
    @param rdev             Attribute of the newly created node

    @return                 mknod(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_mknod (const char *path, mode_t mode, dev_t rdev)
{
    set_permissions ();
    return -EACCES;
}

/**
    Creates a new folder. This is possible only in temporary paths

    @param path             Path of the new folder
    @param mode             Permissions for the newly created folder

    @return                 mkdir(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_mkdir (const char *path, mode_t mode)
{
    set_permissions ();
    return create_item_by_path (path, NODE_IS_FOLDER, NULL);
}

/**
    Removes a file. This is possible only in temporary paths

    @param path             Path of the file to remove

    @return                 unlink(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_unlink (const char *path)
{
    int ret;
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);

    if (target != NULL) {
        if (item_handler_is_folder (target) == FALSE) {
            item_handler_remove (target);
            nodes_cache_remove_by_path (get_cache_reference (), path);
            ret = 0;
        }
        else {
            ret = -EISDIR;
        }
    }
    else {
        ret = -ENOENT;
    }

    return ret;
}

/**
    Removes a folder and all its contents. This is possible only in temporary paths

    @param path             Path of the directory to remove

    @return                 rmdir(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_rmdir (const char *path)
{
    int ret;
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);

    if (target != NULL && item_handler_is_folder (target)) {
        item_handler_remove (target);
        nodes_cache_remove_by_path (get_cache_reference (), path);
        ret = 0;
    }
    else {
        ret = -ENOTDIR;
    }

    return ret;
}

/**
    Creates a new symlink. This is possible only in temporary paths

    @param from             Path of the linked file
    @param to               Path of the link

    @return                 symlink(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_symlink (const char *from, const char *to)
{
    set_permissions ();

    /**
        TODO    To be implemented
    */
    return -EACCES;
}

/**
    Renames a file. This is possible only in temporary paths

    @param from             Original path of the file
    @param to               Destination path

    @return                 rename(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_rename (const char *from, const char *to)
{
    int res;
    CONTENT_TYPE start_type;
    ItemHandler *start;
    ItemHandler *target;
    HierarchyNode *start_level;
    HierarchyNode *target_level;

    set_permissions ();

    /*
        Into the effective hierarchy, an existing item can only be moved as another valid
        item. The procedure of metadata guessing in function of the hierarchy path is
        re-executed
    */
    start = verify_exposed_path (from);
    if (start == NULL)
        return -ENOENT;

    /*
        If both paths, origin and destination, refer to something into a mirror folder, so are
        just maps to the real filesystem, a normal rename() is called so to avoid many (useless)
        handling
    */
    start_type = item_handler_get_format (start);
    if ((start_type == ITEM_IS_MIRROR_ITEM || start_type == ITEM_IS_MIRROR_FOLDER)) {
        start_level = item_handler_get_logic_node (start);
        /**
            TODO    Actually this works only for "system_folders" nodes, provide to correct so to
                    be correct also for the "mirror_contents" case
        */
        if (strcmp (hierarchy_node_get_mirror_path (start_level), "/") == 0) {
            target_level = node_at_path (to);
            if (target_level == item_handler_get_logic_node (start)) {
                res = rename (from, to);
                if (res != 0)
                    return -errno;
            }
        }
    }

    target = verify_exposed_path (to);

    if (target == NULL) {
        res = create_item_by_path (to, item_handler_is_folder (start) ? NODE_IS_FOLDER : NODE_IS_FILE, &target);
        if (res != 0)
            return res;
    }

    replace_hierarchy_node (start, target);
    nodes_cache_remove_by_path (get_cache_reference (), from);
    return 0;
}

/**
    Creates a new hard link. This is possible only in temporary paths

    @param from             Path of the linked file
    @param to               Path of the link

    @return                 link(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_link (const char *from, const char *to)
{
    set_permissions ();

    /**
        TODO    To be implemented
    */
    return -EACCES;
}

/**
    Change permissions for a file. This is possible only in temporary paths

    @param path             Path of the file for which modify permissions
    @param mode             New privileges mask to apply

    @return                 chmod(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_chmod (const char *path, mode_t mode)
{
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);
    return item_handler_chmod (target, mode);
}

/**
    Change ownership for a file. This is possible only in temporary paths

    @param path             Path of the file for which modify owner
    @param uid              UID of the new user owner
    @param gid              GID of the new group owner

    @return                 chown(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_chown (const char *path, uid_t uid, gid_t gid)
{
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);
    return item_handler_chown (target, uid, gid);
}

/**
    Truncate a file to a specified length

    @param path             Path of the file to truncate
    @param size             New size for the file

    @return                 truncate()
*/
static int ifs_truncate (const char *path, off_t size)
{
    ItemHandler *target;

    set_permissions ();
    target = verify_exposed_path (path);
    return item_handler_truncate (target, size);
}

/**
    Truncate a file to a specified length

    @param path             Path of the file to truncate
    @param size             New size for the file
    @param fi               Informations about the file to truncate

    @return                 ftruncate()
*/
static int ifs_ftruncate (const char *path, off_t size, struct fuse_file_info *fi)
{
    int ret;
    OpenedItem *item;

    FI_TO_OPENED_ITEM (fi, item);

    if (item == NULL) {
        /*
            Just as a safety belt: if something happened to the file
            descriptor, lets truncate the file using the path
        */
        ret = ifs_truncate (path, size);
    }
    else {
        set_permissions ();
        ret = ftruncate (item->fd, size);
    }

    return ret;
}

/**
    Change file last access and modification times

    @param path             Path of the file for which modify times
    @param ts               New times

    @return                 utimes()
*/
static int ifs_utimens (const char *path, const struct timespec ts[2])
{
    struct timeval tv [2];
    ItemHandler *target;

    tv [0].tv_sec = ts [0].tv_sec;
    tv [0].tv_usec = ts [0].tv_nsec / 1000;
    tv [1].tv_sec = ts [1].tv_sec;
    tv [1].tv_usec = ts [1].tv_nsec / 1000;

    set_permissions ();
    target = verify_exposed_path (path);
    return item_handler_utimes (target, tv);
}

/**
    Opens a file for read and write

    @param path             Path of the file to open
    @param fi               Informations about the opening action, and filled with addictional
                            data to handle while read(), write() and close()

    @return                 0 if successfull, a negative value otherwise
*/
static int ifs_open (const char *path, struct fuse_file_info *fi)
{
    int res;
    ItemHandler *target;
    OpenedItem *item;

    set_permissions ();

    target = verify_exposed_path (path);
    if (target == NULL)
        return -ENOENT;

    res = item_handler_open (target, fi->flags);
    if (res < 0)
        return res;

    item = allocate_opened_item (target, res);
    if (item == NULL) {
        /*
            In absence of a more specific error, here return the one which is fault by design :-P
            cfr. man 2 open
        */
        return -ENODEV;
    }

    OPENED_ITEM_TO_FI (item, fi);
    return 0;
}

/**
    Creates a new file

    @param path             Path for the new file
    @param mask             Permissions mask for the new element
    @param fi               Informations about the creating action, and filled with addictional
                            data to handle while read(), write() and close()

    @return                 0 if successfull, a negative value otherwise
*/
static int ifs_create (const char *path, mode_t mask, struct fuse_file_info *fi)
{
    int res;
    ItemHandler *target;
    OpenedItem *item;

    set_permissions ();
    res = create_item_by_path (path, NODE_IS_FILE, &target);
    if (res != 0)
        return res;

    res = item_handler_open (target, fi->flags & ~O_CREAT);
    if (res < 0)
        return res;

    item = allocate_opened_item (target, res);
    if (item == NULL)
        return -ENODEV;

    OPENED_ITEM_TO_FI (item, fi);
    return 0;
}

/**
    Read bytes from an opened file

    @param path             Path of the file from which read data
    @param buf              Buffer to fill with read bytes
    @param size             Size of the allocation of "buf"
    @param offset           Starting position for the read
    @param fi               Contains informations about the opened file, such as assigned in
                            ifs_open() or ifs_create()

    @return                 pread()
*/
static int ifs_read (const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    int res;
    OpenedItem *item;

    set_permissions ();

    FI_TO_OPENED_ITEM (fi, item);
    if (item == NULL)
        return -EBADF;

    res = pread (item->fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

/**
    Write bytes to an opened file

    @param path             Path of the file in which write data
    @param buf              Buffer with bytes to write
    @param size             Size of the allocation of "buf"
    @param offset           Starting position for the write
    @param fi               Contains informations about the opened file, such as assigned in
                            ifs_open() or ifs_create()

    @return                 pwrite()
*/
static int ifs_write (const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    int res;
    OpenedItem *item;

    set_permissions ();

    FI_TO_OPENED_ITEM (fi, item);
    if (item == NULL)
        return -EBADF;

    /*
        Remember about splice(2) for future COW implementation
    */

    res = pwrite (item->fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

/**
    Retrieve informations about the filesystem

    @param path             Path of a reference file
    @param stbuf            Structure to be filled with informations about the filesystem
*/
static int ifs_statfs (const char *path, struct statvfs *stbuf)
{
    int res;

    set_permissions ();

    /**
        TODO    Customize data into stbuf
    */

    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

/**
    To flush an opened file

    @param path             Path of the file to flush
    @param fi               Informations about the opened file

    @return                 0 if successful, otherwise a negative value describing the error
*/
static int ifs_flush (const char *path, struct fuse_file_info *fi)
{
    OpenedItem *item;

    FI_TO_OPENED_ITEM (fi, item);
    if (item == NULL)
        return -EBADF;

    set_permissions ();

    if (fsync (item->fd) != 0)
        return errno * -1;

    return 0;
}

/**
    Closes a file opened with ifs_open() or ifs_create()

    @param path             Path of the file to close
    @param fi               Informations about the opened file

    @return                 close()
*/
static int ifs_release (const char *path, struct fuse_file_info *fi)
{
    int res;
    OpenedItem *item;

    FI_TO_OPENED_ITEM (fi, item);
    if (item == NULL)
        return -EBADF;

    set_permissions ();

    if (item->item != NULL) {
        item_handler_close (item->item, item->fd);
        res = 0;
    }
    else {
        res = close (item->fd);
    }

    free_opened_item (item);
    fi->fh = 0;
    return res;
}

/**
    Requires a permanent write of buffered data on the disk

    @param path             Path of the file to sync
    @param isdatasync       Unused
    @param fi               Informations about the opened file
*/
static int ifs_fsync (const char *path, int isdatasync, struct fuse_file_info *fi)
{
    OpenedItem *item;

    FI_TO_OPENED_ITEM (fi, item);
    if (item == NULL)
        return -EBADF;

    set_permissions ();
    return fsync (item->fd);
}

static gchar* read_configuration (int *fsize)
{
    gchar *offset;
    gchar *string;
    gchar *comment_start;
    gchar *comment_end;
    FILE *f;

    f = fopen (Config.conf_file, "rb");
    fseek (f, 0, SEEK_END);
    *fsize = (int) ftell (f);
    fseek (f, 0, SEEK_SET);

    string = g_malloc (*fsize + 1);
    fread (string, *fsize, 1, f);
    fclose (f);

    string [*fsize] = 0;

    offset = string;

    while (TRUE) {
        comment_start = strstr (offset, "<!--");
        if (comment_start == NULL)
            break;

        comment_end = strstr (comment_start, "-->");
        if (comment_end == NULL)
            break;

        while (comment_start != comment_end + 3) {
            *comment_start = ' ';
            comment_start++;
        }

        offset = comment_start;
    }

    return string;
}

/**
    Provides to parse a configuration file
*/
static void check_configuration ()
{
    int fsize;
    gchar *file;
    xmlDocPtr doc;

    file = read_configuration (&fsize);
    doc = xmlReadMemory (file, fsize, NULL, NULL, XML_PARSE_NOBLANKS);

    if (doc == NULL) {
        g_warning ("Unable to read configuration");
    }
    else {
        build_hierarchy_tree_from_xml (doc);
        xmlFreeDoc (doc);
    }

    g_free (file);
}

/**
    Init the filesystem, retriving contents from Item Manager

    @param conn             Unused
*/
static void* ifs_init (struct fuse_conn_info *conn)
{
    check_configuration ();

    /*
        User parameters are directly embedded in the hierarchy tree (cfr.
        parse_reference_formula()), so we can destroy the table at the end of the configuration
        file parsing
    */
    set_user_param (NULL, NULL);

    return NULL;
}

/**
    Uninit the filesystem, destroying local tree of contents got from Item Manager

    @param conn             Unused
*/
static void ifs_destroy (void *conn)
{
    g_main_loop_quit (g_main_loop_new (NULL, FALSE));
    destroy_hierarchy_tree ();
    free_conf ();
}

/**
    Map of the functions in this implementation against callbacks struct handled by FUSE
*/
static struct fuse_operations ifs_oper = {
    .init           = ifs_init,
    .destroy        = ifs_destroy,
    .getattr        = ifs_getattr,
    .access         = ifs_access,
    .readlink       = ifs_readlink,
    .readdir        = ifs_readdir,
    .mknod          = ifs_mknod,
    .mkdir          = ifs_mkdir,
    .symlink        = ifs_symlink,
    .unlink         = ifs_unlink,
    .rmdir          = ifs_rmdir,
    .rename         = ifs_rename,
    .link           = ifs_link,
    .chmod          = ifs_chmod,
    .chown          = ifs_chown,
    .create         = ifs_create,
    .ftruncate      = ifs_ftruncate,
    .truncate       = ifs_truncate,
    .utimens        = ifs_utimens,
    .open           = ifs_open,
    .read           = ifs_read,
    .write          = ifs_write,
    .statfs         = ifs_statfs,
    .flush          = ifs_flush,
    .release        = ifs_release,
    .fsync          = ifs_fsync

    /**
        TODO    Add .ioctl() handler, and add support for BTRFS_IOC_CLONE (copy-on-write ioctl)
                http://sourceforge.net/mailarchive/forum.php?thread_name=87mxzzinek.fsf%40inspiron.ap.columbia.edu&forum_name=fuse-devel
                http://git.savannah.gnu.org/gitweb/?p=coreutils.git;a=blob;f=src/copy.c;h=80ec3625e11ba99b3239a474494e087282b2908e;hb=HEAD
    */
};

static void usage ()
{
    fprintf (stderr,
"general options:\n"
"    -o opt,[opt...]        mount options\n"
"    -h   --help            print help\n"
"    -V   --version         print version\n"
"\n"
"FSter options:\n"
"   -c FILE                 specify a configuration file (default " DEFAULT_CONFIG_FILE ")\n"
"   -p NAME=VALUE           specify value for a user parameter found in configuration file\n"
"\n");
}

static int fster_opt_proc (void *data, const char *arg, int key, struct fuse_args *outargs)
{
    gchar *param_name = NULL;
    gchar *param_value = NULL;

    switch (key) {
        case KEY_HELP:
            usage ();
            fuse_opt_add_arg (outargs, "-ho");
            fuse_main (outargs->argc, outargs->argv, &ifs_oper, NULL);
            free_conf ();
            exit (0);
            break;

        case KEY_CONFIGFILE:
            Config.conf_file = g_strdup (arg + 2);
            break;

        case KEY_VERSION:
            printf ("FSter version " VERSION "\n");
            exit (0);
            break;

        case KEY_USER_PARAMETER:
            if (strchr (arg + 2, '=') == NULL) {
                g_warning ("Malformed parameter, should be name=value");
                free_conf ();
                exit (1);
            }

            param_name = g_strdup (arg + 2);
            param_value = strchr (param_name, '=');
            *param_value = '\0';
            param_value = g_strdup (param_value + 1);
            set_user_param (param_name, param_value);
            break;

        default:
            return 1;
            break;
    }

    return 0;
}

/**
    Main of the program
*/
int main (int argc, char *argv [])
{
    GMainLoop *gloop;
    GFuseLoop *loop;
    struct fuse_args args = FUSE_ARGS_INIT (argc, argv);

    umask (0);
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

    memset (&Config, 0, sizeof (Config));

    if (fuse_opt_parse (&args, &Config, fster_opts, fster_opt_proc) == -1) {
        free_conf ();
        exit (1);
    }

    if (Config.conf_file == NULL)
        Config.conf_file = g_strdup (DEFAULT_CONFIG_FILE);

    if (access (Config.conf_file, F_OK | R_OK) != 0) {
        g_warning ("Unable to find configuration file in %s", Config.conf_file);
        free_conf ();
        exit (1);
    }

    loop = gfuse_loop_new ();
    gfuse_loop_set_operations (loop, &ifs_oper);
    gfuse_loop_set_config (loop, args.argc, args.argv);
    gfuse_loop_run (loop);

    gloop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (gloop);

    fuse_opt_free_args (&args);
    g_object_unref (loop);
    g_object_unref (gloop);

    exit (0);
}
