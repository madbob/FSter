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

#include "common.h"
#include "hierarchy.h"

#define DEFAULT_CONFIG_FILE         "/etc/guglielmo/avfs/avfs.xml"

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
    KEY_CONFIGFILE
};

static struct fuse_opt filer_opts [] = {
    FUSE_OPT_KEY ("-c ",        KEY_CONFIGFILE),
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
    g_free (Config.conf_file);
}

/**
    FIXME   Document this
*/
static int create_item_by_path (const gchar *path, NODE_TYPE type, ItemHandler **target)
{
    int parent_type;
    gchar *name;
    ItemHandler *item;

    item = verify_exposed_path (path);
    if (item != NULL)
        return -EEXIST;

    name = g_path_get_dirname (path);
    item = verify_exposed_path (name);
    g_free (name);

    if (item == NULL)
        return -ENOTDIR;

    parent_type = item_handler_get_format (item);
    if (parent_type != ITEM_IS_VIRTUAL_FOLDER && parent_type != ITEM_IS_SHADOW_FOLDER && parent_type != ITEM_IS_STATIC_FOLDER)
        return -ENOTDIR;

    name = g_path_get_basename (path);
    item = item_handler_attach_child (item, type, name);
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

                if (item_handler_stat (child, &st) == 0)
                    ptr_st = &st;
                else
                    ptr_st = NULL;

                name = item_handler_exposed_name (child);
                if (name == NULL)
                    continue;

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
    ItemHandler *target;

    target = verify_exposed_path (path);
    item_handler_remove (target);
    return 0;
}

/**
    Removes a folder and all its contents. This is possible only in temporary paths

    @param path             Path of the directory to remove

    @return                 rmdir(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_rmdir (const char *path)
{
    ItemHandler *target;

    target = verify_exposed_path (path);

    if (item_handler_is_folder (target)) {
        item_handler_remove (target);
        return 0;
    }
    else
        return -ENOTDIR;
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
    ItemHandler *start;
    ItemHandler *target;

    res = -EACCES;

    /*
        Into the effective hierarchy, an existing item can only be moved as another valid
        item. The procedure of metadata guessing in function of the hierarchy path is
        re-executed
    */
    start = verify_exposed_path (from);
    if (start == NULL)
        return -ENOENT;

    target = verify_exposed_path (to);
    if (target == NULL) {
        res = create_item_by_path (to, item_handler_is_folder (start) ? NODE_IS_FOLDER : NODE_IS_FILE, &target);
        if (res != 0)
            return res;
    }

    replace_hierarchy_node (start, target);
    return res;
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
    /**
        TODO    To be implemented
    */
    return -EACCES;
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
    /**
        TODO    To be implemented
    */
    return -EACCES;
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
    if (item == NULL)
        return -EBADF;

    ret = ftruncate (item->fd, size);
    return ret;
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

    target = verify_exposed_path (path);
    return item_handler_truncate (target, size);
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

    target = verify_exposed_path (path);
    if (target == NULL)
        return -ENOENT;

    res = item_handler_open (target, fi->flags);

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

    res = create_item_by_path (path, NODE_IS_FILE, &target);
    if (res != 0)
        return res;

    res = item_handler_open (target, fi->flags);

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

    /**
        TODO    Customize data into stbuf
    */

    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

/**
    FIXME   Document this
*/
static int ifs_flush (const char *path, struct fuse_file_info *fi)
{
    OpenedItem *item;

    FI_TO_OPENED_ITEM (fi, item);
    if (item == NULL)
        return -EBADF;

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

    return fsync (item->fd);
}

/**
    Provides to parse a configuration file
*/
static void check_configuration ()
{
    xmlDocPtr doc;

    if (Config.conf_file == NULL)
        Config.conf_file = g_strdup (DEFAULT_CONFIG_FILE);

    if (access (Config.conf_file, F_OK | R_OK) != 0) {
        g_warning ("Unable to find configuration file in %s", Config.conf_file);
    }
    else {
        doc = xmlReadFile (Config.conf_file, NULL, XML_PARSE_NOBLANKS);

        if (doc == NULL) {
            g_warning ("Unable to read configuration");
        }
        else {
            build_hierarchy_tree_from_xml (doc);
            xmlFreeDoc (doc);
        }
    }
}

/**
    Init the filesystem, retriving contents from Item Manager

    @param conn             Unused
*/
static void* ifs_init (struct fuse_conn_info *conn)
{
    check_configuration ();
    return NULL;
}

/**
    Uninit the filesystem, destroying local tree of contents got from Item Manager

    @param conn             Unused
*/
static void ifs_destroy (void *conn)
{
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
    .fsync          = ifs_fsync,
};

static int filer_opt_proc (void *data, const char *arg, int key, struct fuse_args *outargs)
{
    switch (key) {
        case KEY_CONFIGFILE:
            Config.conf_file = g_strdup (arg + 2);
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
    struct fuse_args args = FUSE_ARGS_INIT (argc, argv);

    umask (0);
    g_type_init ();
    memset (&Config, 0, sizeof (Config));

    if (fuse_opt_parse (&args, &Config, filer_opts, filer_opt_proc) == -1) {
        free_conf ();
        exit (1);
    }

    fuse_main (args.argc, args.argv, &ifs_oper, NULL);
    fuse_opt_free_args (&args);
    exit (0);
}
