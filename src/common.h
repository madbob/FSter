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

#ifndef COMMON_INTERNALS_H
#define COMMON_INTERNALS_H

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <regex.h>
#include <dirent.h>
#include <libgen.h>
#include <pthread.h>
#include <errno.h>
#include <dlfcn.h>
#include <ctype.h>
#include <attr/xattr.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <gio/gio.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#define TRACKER_ENABLE_INTERNALS
#include <libtracker-common/tracker-common.h>
#undef TRACKER_ENABLE_INTERNALS

#endif
