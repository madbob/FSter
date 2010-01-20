/*  Copyright (C) 2010 Itsme S.r.L.
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

#ifndef GFUSE_LOOP_H
#define GFUSE_LOOP_H

#include "core.h"
#include "common.h"

#define GFUSE_LOOP_TYPE             (gfuse_loop_get_type ())
#define GFUSE_LOOP(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                                     GFUSE_LOOP_TYPE, GFuseLoop))
#define GFUSE_LOOP_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  \
                                     GFUSE_LOOP_TYPE,                   \
                                     GFuseLoopClass))
#define IS_GFUSE_LOOP(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                     GFUSE_LOOP_TYPE))
#define IS_GFUSE_LOOP_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  \
                                     GFUSE_LOOP_TYPE))
#define GFUSE_LOOP_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  \
                                     GFUSE_LOOP_TYPE,                   \
                                     GFuseLoopClass))

typedef struct _GFuseLoop         GFuseLoop;
typedef struct _GFuseLoopClass    GFuseLoopClass;
typedef struct _GFuseLoopPrivate  GFuseLoopPrivate;

struct _GFuseLoop {
    GObject             parent;
    GFuseLoopPrivate    *priv;
};

struct _GFuseLoopClass {
    GObjectClass    parent_class;
};

GType           gfuse_loop_get_type         ();

GFuseLoop*      gfuse_loop_new              ();
void            gfuse_loop_set_operations   (GFuseLoop *loop, struct fuse_operations *operations);
void            gfuse_loop_set_config       (GFuseLoop *loop, int argc, gchar **argv);
void            gfuse_loop_run              (GFuseLoop *loop);

GFuseLoop*      gfuse_loop_get_current      ();
const gchar*    gfuse_loop_get_mountpoint   (GFuseLoop *loop);
void*           gfuse_loop_get_private      (GFuseLoop *loop);

#endif
