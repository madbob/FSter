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

#ifndef __COMMON_AVFS_INTERNALS_H__
#define __COMMON_AVFS_INTERNALS_H__

#define FUSE_USE_VERSION 26

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <regex.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <attr/xattr.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <gio/gio.h>
#include <guglielmo-glib.h>

/**
    Max filename lenght, is the same of EXT4
*/
#define MAX_NAMES_SIZE      256

typedef struct _WrappedItem     WrappedItem;

typedef enum {
    IS_IN_HOME_FOLDER,
    IS_ARBITRARY_PATH,
    IS_HIERARCHY_FOLDER,
    IS_HIERARCHY_LEAF,
    IS_HIERARCHY_EXTENSION,
    IS_MISSING_HIERARCHY_LEAF,
    IS_MISSING_HIERARCHY_EXTENSION,
} HIERARCHY_LEVEL;

#endif
