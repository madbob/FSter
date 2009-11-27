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

/**
    Assigns 0 to a pointer, if the pointer is not NULL
*/
#define ZERO_INT_PTR(__ptr) {   \
    if (__ptr != NULL)          \
        *__ptr = 0;             \
}

/**
    Assigns NULL to a double pointer, if the pointer is not NULL
*/
#define ZERO_PTR(__ptr) {   \
    if (__ptr != NULL)      \
        *__ptr = NULL;      \
}

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

/**
    Provide yourself a cup of coffee and sit down...
    The problem was retrived using OpenOffice (but probably is common to other packages): when
    saving a file the application creates an extra hidden file used to handle locks, in the same
    folder. But certain folders in the hierarchy are virtual, nothing may be saved inside them,
    and it is not a good idea di create a true Item just for a lock file.
    Fortunately those special files can be catched due the fact they are hidden, and they are
    handled in special way: this macro provides them a new path in /tmp folder, but same
    filename, and everytime an hidden file referring something inside a folder and not matching
    any node in the hierarchy is required this is applied. This way lock files are safely saved
    in /tmp, and is always possible to retrieve them (and delete when opportune).
    Please note this is an expression macro, do not remove round brackets
*/
#define TEMP_PARANOID_PATH(__path) ({       \
    char *__tmp;                            \
    __tmp = alloca(strlen (__path) + 5);    \
    sprintf (__tmp, "/tmp/%s", __path);     \
    __tmp;                                  \
})

/**
    The file has been written, so it will be required to inform Item Manager on close to force
    re-indexing
*/
#define OPENED_ITEM_WRITTEN         0x0001

/**
    Wrapper around Item, contains some informations about opening status of files on the
    filesystem
*/
typedef struct {
    WrappedItem *item;                      /**< Reference to the opened Item */
    int         fd;                         /**< Opened file descriptor, as returned by open() against the real path of "item" */
    int         status;                     /**< Status of the opened file, contains a bitwise combination of above listed flags */
} OpenedItem;

/**
    Used as parameter for advanced search operations which require use of *_foreach() functions
    and dedicated matching callbacks
*/
typedef struct {
    Item        *item;                      /**< Generic reference to a Item */
    gchar       *param;                     /**< Generic reference to a string */
} SearchReference;

/**
    This dummy structure holds parameters used while building responses to readdir() operations:
    it may be passed to *_foreach_*() functions
*/
typedef struct {
    fuse_fill_dir_t     filler;             /**< Callback to fill buffer with retrieved contents */
    void                *buffer;            /**< Buffer to fill with the "filler" callback */
} FolderLookupInfo;

/**
    This is to evaluate the name of a file, implements a simple heuristic to determine if it is a
    regular file (to be indexed in Guglielmo storage) or simply a temporary file created by the
    user application (not to be indexed)

    @param name             File name to evaluate

    @return                 TRUE is the file name is considered temporary, FALSE otherwise
*/
static gboolean is_a_temporary_file (const gchar *name)
{
    return ((name [0] == '.') || (name [0] == '~') || (name[strlen (name) - 1] == '~'));
}

/**
    Check if the provided path is accessible without restrictions imposed by all other parts of
    the filesystem

    @param path             Path to check

    @return                 TRUE if the path is freely accessible and user applications can read
                            and write with no underlying limits, 0 otherwise
*/
static gboolean path_is_freely_accessible (const char *path)
{
    int ret;
    char name [MAX_NAMES_SIZE];
    HIERARCHY_LEVEL position;
    WrappedItem *target;

    /*
        /tmp folder is always accessible
    */
    if (strncmp (path, "/tmp", strlen("/tmp")) == 0)
        return TRUE;

    /*
        /dev folder is always accessible
    */
    if (strncmp (path, "/dev", strlen("/dev")) == 0)
        return TRUE;

    /*
        /usr folder is always accessible.
        This is required due the fact lot of applications use to read in folders looking for
        file, instead of know paths a priori, so lookup have to be permitted
    */
    if (strncmp (path, "/usr", strlen("/usr")) == 0)
        return TRUE;

    ret = FALSE;

    memset (name, 0, MAX_NAMES_SIZE);
    position = verify_exposed_path (path, &target, name);

    switch (position) {
        case IS_MISSING_HIERARCHY_LEAF:
            if (is_a_temporary_file (name))
                ret = TRUE;
            break;

        default:
            break;
    }

    return ret;
}

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

    @param item             WrappedItem to be added to the list, or NULL if a regular object is
                            opened
    @param fd               File descriptor opened over the Item file

    @return                 Newly allocated OpenedItem, to free with free_opened_item()
*/
static inline OpenedItem* allocate_opened_item (WrappedItem *item, int fd)
{
    OpenedItem *newer;

    newer = calloc (1, sizeof (OpenedItem));
    if (newer == NULL)
        return NULL;

    newer->item = item;
    newer->fd = fd;
    return newer;
}

/**
    Retrieve informations about a file

    @param path             Path of the file to check out
    @param stbuf            Struct to fill with informations

    @return                 0 if successful, otherwise a negative value describing the error
*/
static int ifs_getattr (const char *path, struct stat *stbuf)
{
    int res;
    char *home;
    char name [MAX_NAMES_SIZE];
    WrappedItem *target;

    fflush (stdout);

    res = verify_exposed_path (path, &target, name);

    switch (res) {
        case IS_ARBITRARY_PATH:
            /*
                If any other file is required, a real lstat() is performed
            */
            res = lstat (path, stbuf);
            if (res == -1)
                res = -errno;

            break;

        case IS_IN_HOME_FOLDER:
            home = getenv ("HOME");
            if (home == NULL)
                res = -EACCES;
            else
                res = lstat (home, stbuf);

            break;

        case IS_MISSING_HIERARCHY_LEAF:
            if (is_a_temporary_file (name)) {
                path = TEMP_PARANOID_PATH (name);
                res = lstat (path, stbuf);
                if (res == -1)
                    res = -errno;
            }
            else
                res = -ENOENT;

            break;

        case IS_HIERARCHY_FOLDER:
            home = getenv ("HOME");

            if (home == NULL) {
                res = -EACCES;
            }
            else {
                res = lstat (home, stbuf);
                if (res == -1)
                    res = -errno;
            }

            break;

        case IS_HIERARCHY_EXTENSION:
        case IS_HIERARCHY_LEAF:
            home = (gchar*) get_real_path (target);

            if (home == NULL) {
                res = -ENOENT;
            }
            else {
                res = lstat (home, stbuf);
                if (res == -1)
                    res = -errno;
            }

            break;

        default:
            res = -ENOENT;
            break;
    }

    fflush (stdout);
    return res;
}

/**
    Check permissions for a file

    @param path             Path of the file for which check permissions
    @param mask             Permissions mask to check

    @return                 access()
*/
static int ifs_access (const char *path, int mask)
{
    int res;
    char *home;
    char name [MAX_NAMES_SIZE];
    const char *real_path;
    WrappedItem *target;

    res = verify_exposed_path (path, &target, name);

    switch (res) {
        case IS_ARBITRARY_PATH:
            real_path = path;
            break;

        case IS_MISSING_HIERARCHY_LEAF:
            if (is_a_temporary_file (name))
                real_path = TEMP_PARANOID_PATH (name);
            else
                return -ENOENT;

            break;

        case IS_IN_HOME_FOLDER:
        case IS_HIERARCHY_FOLDER:
            home = getenv ("HOME");
            if (home == NULL)
                return -EACCES;

            real_path = (const char*) home;
            break;

        case IS_HIERARCHY_LEAF:
            real_path = get_real_path (target);
            break;

        default:
            return -ENOENT;
            break;
    }

    res = access (real_path, mask);
    if (res == -1)
        return -errno;

    return 0;
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
    int res;
    char name [MAX_NAMES_SIZE];
    const char *real_path;
    WrappedItem *target;

    res = verify_exposed_path (path, &target, name);

    switch (res) {
        case IS_ARBITRARY_PATH:
            real_path = path;
            break;

        default:
            return -EACCES;
            break;
    }

    res = readlink (path, buf, size - 1);

    if (res == -1) {
        res = -errno;
    }
    else {
        buf[res] = '\0';
        res = 0;
    }

    return res;
}

/**
    Retrive the stat() information of a file

    @param path             Folder of the file
    @param path_len         Lenght of "path"
    @param file_name        Name of the file to stat
    @param st               Struct to be filled with stat()
*/
static void stat_file (const char *path, int path_len, const char *file_name, struct stat *st)
{
    int file_path_len;
    char *file_path;

    file_path_len = path_len + strlen (file_name) + 5;       // To be large ;-)
    file_path = alloca (file_path_len);
    snprintf (file_path, file_path_len, "%s/%s", path, file_name);
    stat (file_path, st);
}

/**
    Provide to read files from a normal folder, and fill the output buffer destinated to user
    application

    @param path             Path to read
    @param buf              Buffer to fill, is used as parameter of "filler"
    @param filler           Callback to call for each found file
    @param offset           Starting offset for reading
    @param filled           If != NULL, it is set to the number of red files

    @return                 0 if successfull, or a negative value
*/
static int readdir_normal (const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, int *filled)
{
    register int i;
    int ret;
    int path_len;
    struct stat st;
    struct dirent entry;
    struct dirent *ret_buffer;
    DIR *dir;

    dir = opendir (path);
    if (dir == NULL) {
        ZERO_INT_PTR (filled);
        return -errno;
    }

    ret = -1;

    for (i = 0; i < offset; i++) {
        if (readdir_r (dir, &entry, &ret_buffer) != 0) {
            ZERO_INT_PTR (filled);
            ret = -errno;
            break;
        }

        if (ret_buffer == NULL) {
            ret = 0;
            break;
        }
    }

    /*
        Read continues also if no more files have to be collected from readdir() (i == offset):
        this is intentional, because in the if() we set some more variables and it is useless to
        reproduce that code
    */

    if (ret == -1) {
        path_len = strlen (path);
        i = 0;

        while (readdir_r (dir, &entry, &ret_buffer) == 0 && ret_buffer != NULL) {
            stat_file (path, path_len, entry.d_name, &st);
            if (filler (buf, entry.d_name, &st, 0))
                break;

            i++;
        }

        ret = 0;

        if (filled != NULL)
            *filled = i;
    }

    closedir (dir);
    return ret;
}

/**
    Used to retrieve informations about a leaf in the hierarchy: used as complement of
    ifs_readdir()

    @param item             An Item
    @param data             Pointer to a FolderLookupInfo structure, which holds informations to
                            build the response to the readdir() request

    @return                 TRUE until the filler callback (in "data") returns 0 and space for
                            other entries is available, FALSE otherwise
*/
static gboolean filling_with_items_name (WrappedItem *item, gpointer data)
{
    struct stat st;
    struct stat *ptr_st;
    FolderLookupInfo *info;
    const gchar *str;
    const gchar *name;

    info = (FolderLookupInfo*) data;

    str = get_real_path (item);

    if ((str != NULL) && (stat (str, &st) == 0))
        ptr_st = &st;
    else
        ptr_st = NULL;

    name = get_exposed_name (item);
    if (name == NULL)
        name = "";

    if (info->filler (info->buffer, name, ptr_st, 0))
        return FALSE;
    else
        return TRUE;
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
    int res;
    int ret;
    WrappedItem *target;
    FolderLookupInfo filling_info;

    filling_info.buffer = buf;
    filling_info.filler = filler;

    res = verify_exposed_path (path, &target, NULL);

    switch (res) {
        case IS_ARBITRARY_PATH:
            ret = readdir_normal (path, buf, filler, offset, NULL);
            break;

        case IS_IN_HOME_FOLDER:
            hierarchy_foreach_content (NULL, filling_with_items_name, &filling_info);
            ret = 0;
            break;

        case IS_HIERARCHY_FOLDER:
        case IS_HIERARCHY_EXTENSION:
            hierarchy_foreach_content (target, filling_with_items_name, &filling_info);
            ret = 0;
            break;

        case IS_HIERARCHY_LEAF:
            ret = -ENOTDIR;
            break;

        default:
            ret = -ENOENT;
            break;
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
    int res;

    res = -EACCES;

    if (path_is_freely_accessible (path)) {
        res = mknod(path, mode, rdev);
        if (res == -1)
            res = -errno;
    }

    return res;
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
    int res;
    WrappedItem *target;

    res = verify_exposed_path (path, &target, NULL);

    switch (res) {
        case IS_ARBITRARY_PATH:
            if (path_is_freely_accessible (path)) {
                res = mkdir (path, mode);
                if (res == -1)
                    res = -errno;
            }
            else {
                res = -EACCES;
            }

            break;

        case IS_MISSING_HIERARCHY_LEAF:
        case IS_MISSING_HIERARCHY_EXTENSION:
            res = ((create_hierarchy_node (target, path, NODE_FOLDER) == NULL) ? -EFAULT : 0);
            break;

        default:
            res = -EACCES;
            break;
    }

    return res;
}

/**
    Removes a file. This is possible only in temporary paths

    @param path             Path of the file to remove

    @return                 unlink(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_unlink (const char *path)
{
    int res;
    WrappedItem *target;

    res = verify_exposed_path (path, &target, NULL);

    switch (res) {
        case IS_ARBITRARY_PATH:
            if (path_is_freely_accessible (path)) {
                res = unlink (path);
                if (res == -1)
                    res = -errno;
            }
            else {
                res = -EACCES;
            }

            break;

        case IS_HIERARCHY_LEAF:
        case IS_HIERARCHY_EXTENSION:
            remove_hierarchy_node (target);
            res = 0;
            break;

        default:
            res = -EACCES;
            break;
    }

    return res;
}

/**
    Removes a folder and all its contents. This is possible only in temporary paths

    @param path             Path of the directory to remove

    @return                 rmdir(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_rmdir (const char *path)
{
    int res;

    res = -EACCES;

    if (path_is_freely_accessible (path)) {
        res = rmdir (path);
        if (res == -1)
            res = -errno;
    }

    return res;
}

/**
    Creates a new symlink. This is possible only in temporary paths

    @param from             Path of the linked file
    @param to               Path of the link

    @return                 symlink(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_symlink(const char *from, const char *to)
{
    int res;

    res = -EACCES;

    if (path_is_freely_accessible (from) && path_is_freely_accessible (to)) {
        res = symlink(from, to);
        if (res == -1)
            res = -errno;
    }

    return res;
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
    int fd_start;
    int fd_target;
    char name [MAX_NAMES_SIZE];
    char *buffer;
    struct stat sbuf;
    WrappedItem *start;
    WrappedItem *target;

    res = -EACCES;

    if (path_is_freely_accessible (from) && path_is_freely_accessible (to)) {
        res = rename (from, to);
        if (res == -1)
            res = -errno;
    }
    else {
        /*
            Into the effective hierarchy, an existing item can only be moved as another valid
            item. The procedure of metadata guessing in function of the hierarchy path is
            re-executed
        */
        res = verify_exposed_path (from, &start, name);

        if (res == IS_HIERARCHY_LEAF || res == IS_HIERARCHY_EXTENSION) {
            res = verify_exposed_path (to, &target, NULL);

            switch (res) {
                case IS_HIERARCHY_LEAF:
                case IS_HIERARCHY_EXTENSION:
                    fd_start = hierarchy_node_open (start, O_RDONLY);
                    fd_target = hierarchy_node_open (target, O_WRONLY);

                    fstat (fd_start, &sbuf);
                    buffer = alloca (sizeof (char) * sbuf.st_size);
                    read (fd_start, buffer, sbuf.st_size);
                    write (fd_target, buffer, sbuf.st_size);

                    hierarchy_node_close (start, fd_start, FALSE);
                    hierarchy_node_close (target, fd_target, TRUE);

                    remove_hierarchy_node (start);
                    res = 0;
                    break;

                case IS_MISSING_HIERARCHY_LEAF:
                case IS_MISSING_HIERARCHY_EXTENSION:
                    if (is_a_temporary_file (name)) {
                        res = -EINVAL;
                    }
                    else {
                        modify_hierarchy_node (start, target, to);
                        res = 0;
                    }

                    break;

                default:
                    res = -EINVAL;
                    break;
            }
        }
        else {
            res = -ENOENT;
        }
    }

    return res;
}

/**
    Creates a new hard link. This is possible only in temporary paths

    @param from             Path of the linked file
    @param to               Path of the link

    @return                 link(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_link(const char *from, const char *to)
{
    int res;

    res = -EACCES;

    if (path_is_freely_accessible (from) && path_is_freely_accessible (to)) {
        res = link(from, to);
        if (res == -1)
            res = -errno;
    }

    return res;
}

/**
    Change permissions for a file. This is possible only in temporary paths

    @param path             Path of the file for which modify permissions
    @param mode             New privileges mask to apply

    @return                 chmod(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_chmod(const char *path, mode_t mode)
{
    int res;

    /*
        To avoid notification of errors while copying files into AVFS, a CHMOD operation always
        returns with successfull code but is applied only when executed on a freely accessible
        file (accordly path_is_freely_accessible() )
    */
    res = 0;

    if (path_is_freely_accessible (path)) {
        res = chmod(path, mode);
        if (res == -1)
            res = -errno;
    }

    return res;
}

/**
    Change ownership for a file. This is possible only in temporary paths

    @param path             Path of the file for which modify owner
    @param uid              UID of the new user owner
    @param gid              GID of the new group owner

    @return                 chown(), or -EACCES if the directory is required outside permitted
                            hierarchy
*/
static int ifs_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;

    res = 0;

    if (path_is_freely_accessible (path)) {
        res = lchown(path, uid, gid);
        if (res == -1)
            res = -errno;
    }

    return res;
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
    int res;
    char name[MAX_NAMES_SIZE];
    const char *real_path;
    WrappedItem *target;

    res = verify_exposed_path (path, &target, name);

    switch (res) {
        case IS_ARBITRARY_PATH:
            real_path = path;
            break;

        case IS_MISSING_HIERARCHY_LEAF:
            if (is_a_temporary_file (name))
                real_path = TEMP_PARANOID_PATH (name);
            else
                return -EACCES;

            break;

        case IS_IN_HOME_FOLDER:
        case IS_HIERARCHY_FOLDER:
            real_path = getenv ("HOME");
            if (real_path == NULL)
                return -ENOENT;

        case IS_HIERARCHY_LEAF:
            real_path = get_real_path (target);
            break;

        default:
            return -ENOENT;
    }

    res = truncate(real_path, size);
    if (res == -1)
        res = -errno;

    return res;
}

/**
    Change file last access and modification times

    @param path             Path of the file for which modify times
    @param ts               New times

    @return                 utimes()
*/
static int ifs_utimens(const char *path, const struct timespec ts[2])
{
    int res;
    char name[MAX_NAMES_SIZE];
    const char *real_path;
    struct timeval tv[2];
    WrappedItem *target;

    res = verify_exposed_path (path, &target, name);

    switch (res) {
        case IS_ARBITRARY_PATH:
            real_path = path;
            break;

        case IS_MISSING_HIERARCHY_LEAF:
            if (is_a_temporary_file (name))
                real_path = TEMP_PARANOID_PATH (name);
            else
                return -EACCES;

            break;

        case IS_IN_HOME_FOLDER:
        case IS_HIERARCHY_FOLDER:
            real_path = getenv ("HOME");
            if (real_path == NULL)
                return -ENOENT;

        case IS_HIERARCHY_EXTENSION:
        case IS_HIERARCHY_LEAF:
            real_path = get_real_path (target);
            break;

        default:
            return -ENOENT;
    }

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    res = utimes(real_path, tv);
    if (res == -1)
        res = -errno;

    return res;
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
    char name [MAX_NAMES_SIZE];
    const char *real_path;
    WrappedItem *target;
    OpenedItem *item;

    res = verify_exposed_path (path, &target, name);

    switch (res) {
        case IS_ARBITRARY_PATH:
            real_path = path;
            target = NULL;
            break;

        case IS_MISSING_HIERARCHY_LEAF:
            if (is_a_temporary_file (name)) {
                real_path = TEMP_PARANOID_PATH (name);
                target = NULL;
            }
            else
                return -EACCES;

            break;

        case IS_HIERARCHY_LEAF:
        case IS_HIERARCHY_EXTENSION:
            res = hierarchy_node_open (target, fi->flags);
            real_path = NULL;
            break;

        default:
            return -ENOENT;
            break;
    }

    if (real_path != NULL) {
        res = open (real_path, fi->flags);
        if (res == -1)
            return -errno;
    }

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
    char *real_path;
    char name [MAX_NAMES_SIZE];
    WrappedItem *target;
    OpenedItem *item;

    res = verify_exposed_path (path, &target, name);

    switch (res) {
        case IS_ARBITRARY_PATH:
            /*
                Outside folders in the hierarchy, creating files is permitted only on permitted
                paths
            */
            if (path_is_freely_accessible (path)) {
                res = creat (path, mask);
                target = NULL;
                if (res == -1)
                    return -errno;
            }
            else
                return -EACCES;

            break;

        case IS_MISSING_HIERARCHY_EXTENSION:
        case IS_MISSING_HIERARCHY_LEAF:
            if (is_a_temporary_file (name)) {
                real_path = TEMP_PARANOID_PATH (name);
                res = creat (real_path, mask);
                target = NULL;
                if (res == -1)
                    return -errno;
            }
            else {
                target = create_hierarchy_node (target, path, NODE_ITEM);
                if (target == NULL)
                    return -errno;

                res = hierarchy_node_open (target, O_RDWR);
            }

            break;

        default:
            return -EACCES;
            break;
    }

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
static int ifs_read(const char *path, char *buf, size_t size, off_t offset,
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
static int ifs_write (const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
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
    else
        item->status |= OPENED_ITEM_WRITTEN;

    return res;
}

/**
    Retrieve informations about the filesystem

    @param path             Path of a reference file
    @param stbuf            Structure to be filled with informations about the filesystem
*/
static int ifs_statfs(const char *path, struct statvfs *stbuf)
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
        hierarchy_node_close (item->item, item->fd, item->status & OPENED_ITEM_WRITTEN);
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
static int ifs_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
    OpenedItem *item;

    FI_TO_OPENED_ITEM (fi, item);
    if (item == NULL)
        return -EBADF;

    return fsync(item->fd);
}

/**
    Provides to parse a configuration file

    @param name             Name of the file to parse. It will be searched in ~/.guglielmo/avfs
*/
static void parse_config_file (gchar *name)
{
    gchar *path;
    xmlDocPtr doc;

    path = getenv ("GUGLIELMO_CFG");

    if (path == NULL)
        path = g_build_filename ("/etc/guglielmo/avfs/", name, NULL);
    else
        path = g_build_filename (path, "/avfs/", name, NULL);

    if (access (path, F_OK | R_OK) != 0) {
        fprintf (stderr, "Unable to find configuration file in %s\n", path);
        g_free (path);
        return;
    }

    doc = xmlReadFile (path, NULL, XML_PARSE_NOBLANKS);

    if (doc == NULL) {
        fprintf (stderr, "Unable to read configuration\n");
    }
    else {
        build_hierarchy_tree_from_xml (doc);
        xmlFreeDoc (doc);
    }

    g_free (path);
}

/**
    Executes parsing of all involved configuration files
*/
static void check_configuration ()
{
    parse_config_file ("avfs.xml");
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
static void ifs_destroy(void *conn)
{
    destroy_hierarchy_tree ();
    guglielmo_core_destroy ();
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

/**
    Main of the program
*/
int main(int argc, char *argv[])
{
    umask(0);

    g_type_init ();
    g_thread_init (NULL);
    guglielmo_core_init ();

    return fuse_main (argc, argv, &ifs_oper, NULL);
}
