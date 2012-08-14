/*  Copyright (C) 2010 Itsme S.r.L.
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

#include "gfuse-loop.h"
#include "fuse_lowlevel.h"

#define GFUSE_LOOP_GET_PRIVATE(obj)       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GFUSE_LOOP_TYPE, GFuseLoopPrivate))

typedef struct {
    GFuseLoop       *loop;
    void            *user;
} PrivateDataBlock;

typedef struct {
    char            *buf;
    int             res;
} ThreadsData;

struct _GFuseLoopPrivate {
    int                     startup_argc;
    gchar                   **startup_argv;

    struct fuse_operations  *real_ops;
    struct fuse_operations  *shadow_ops;
    PrivateDataBlock        *runtime_data;
    gchar                   *mountpoint;
    gboolean                threads;

    GIOChannel              *fuse_fd;
};

G_DEFINE_TYPE (GFuseLoop, gfuse_loop, G_TYPE_OBJECT);

static void gfuse_loop_finalize (GObject *item)
{
    GFuseLoop *loop;

    loop = GFUSE_LOOP (item);

    if (loop->priv->startup_argv != NULL)
        g_free (loop->priv->startup_argv);

    if (loop->priv->fuse_fd != NULL)
        g_io_channel_unref (loop->priv->fuse_fd);

    if (loop->priv->shadow_ops != NULL)
        g_free (loop->priv->shadow_ops);

    if (loop->priv->runtime_data != NULL)
        g_free (loop->priv->runtime_data);
}

static void gfuse_loop_class_init (GFuseLoopClass *klass)
{
    GObjectClass *gobject_class;

    g_type_class_add_private (klass, sizeof (GFuseLoopPrivate));

    gobject_class = G_OBJECT_CLASS (klass);
    gobject_class->finalize = gfuse_loop_finalize;
}

static void gfuse_loop_init (GFuseLoop *item)
{
    item->priv = GFUSE_LOOP_GET_PRIVATE (item);
    memset (item->priv, 0, sizeof (GFuseLoopPrivate));
}

/**
 * gfuse_loop_new:
 *
 * Allocates a new #GFuseLoop
 *
 * Return value: a new #GFuseLoop
 */
GFuseLoop* gfuse_loop_new ()
{
    return g_object_new (GFUSE_LOOP_TYPE, NULL);
}

/**
 * gfuse_loop_set_operations:
 * @loop: a #GFuseLoop
 * @operations: set of callbacks implementing FUSE's actions
 *
 * To associate to @loop a set of functions, just as happens in fuse_main()
 */
void gfuse_loop_set_operations (GFuseLoop *loop, struct fuse_operations *operations)
{
    loop->priv->real_ops = operations;
}

/**
 * gfuse_loop_set_config:
 * @loop: a #GFuseLoop
 * @argc: number of options in @argv
 * @argv: NULL-terminated array of options to pass to libfuse
 *
 * To init the instance of #GFuseLoop with some option, just as happens in
 * fuse_main(). It is suggested to parse the process' argv outside this
 * function to retrieve custom parameters, and pass here only effectively
 * FUSE's valid options
 */
void gfuse_loop_set_config (GFuseLoop *loop, int argc, gchar **argv)
{
    register int i;
    gchar **dup_argv;
    gboolean foreground_option;

    if (loop->priv->startup_argc != 0) {
        struct fuse_args tmp = FUSE_ARGS_INIT (loop->priv->startup_argc, loop->priv->startup_argv);
        fuse_opt_free_args (&tmp);
    }

    foreground_option = FALSE;
    dup_argv = g_new0 (char*, argc + 2);

    for (i = 0; i < argc; i++) {
        dup_argv [i] = argv [i];
        if (strcmp (argv [i], "-f"))
            foreground_option = TRUE;
    }

    /*
        We always force the "foreground" option, to avoid libfuse unattach
        and daemonize the whole process. It is at the toplevel code to decide
        if switch background
    */
    if (foreground_option == FALSE)
        dup_argv [i] = "-f";

    loop->priv->startup_argc = argc + (foreground_option == FALSE ? 1 : 0);
    loop->priv->startup_argv = dup_argv;
}

static inline ThreadsData* do_threads_data (char *buf, int res)
{
    ThreadsData *info;

    info = g_new0 (ThreadsData, 1);
    info->buf = buf;
    info->res = res;
    return info;
}

static inline void free_threads_data (ThreadsData *info)
{
    free (info->buf);
    g_free (info);
}

#if 0

static void manage_request (gpointer data, gpointer user)
{
    struct fuse *fuse;
    struct fuse_session *se;
    struct fuse_chan *ch;
    ThreadsData *info;

    fuse = (struct fuse*) user;
    info = (ThreadsData*) data;

    se = fuse_get_session (fuse);
    ch = fuse_session_next_chan (se, NULL);
    fuse_session_process (se, info->buf, info->res, ch);

    free_threads_data (info);
}

static gboolean manage_fuse_mt (GIOChannel *source, GIOCondition condition, gpointer data)
{
    int res;
    char *buf;
    size_t bufsize;
    struct fuse *fuse;
    struct fuse_session *se;
    struct fuse_chan *ch;
    GThreadPool *pool;
    GError *error;
    ThreadsData *info;

    fuse = (struct fuse*) data;

    error = NULL;
    pool = g_thread_pool_new (manage_request, fuse, -1, FALSE, &error);
    if (pool == NULL) {
        g_warning ("Unable to start thread pool: %s", error->message);
        g_error_free (error);
        return NULL;
    }

    se = fuse_get_session (fuse);
    ch = fuse_session_next_chan (se, NULL);
    bufsize = fuse_chan_bufsize (ch);

    while (1) {
        buf = (char*) malloc (bufsize);
        res = fuse_chan_recv (&ch, buf, bufsize);

        if (res == -EINTR) {
            free (buf);
            continue;
        }
        else if (res <= 0) {
            free (buf);
            break;
        }

        info = do_threads_data (buf, res);

        error = NULL;
        g_thread_pool_push (pool, info, &error);
        if (error != NULL) {
            g_warning ("Unable to start processing request: %s", error->message);
            g_error_free (error);
            free_threads_data (info);
        }
    }

    g_thread_pool_free (pool, TRUE, TRUE);
    return NULL;
}

#endif

static gboolean manage_fuse_st (GIOChannel *source, GIOCondition condition, gpointer data)
{
    int res;
    char *buf;
    gboolean ret;
    size_t bufsize;
    struct fuse *fuse;
    struct fuse_session *se;
    struct fuse_chan *ch;

    fuse = (struct fuse*) data;
    se = fuse_get_session (fuse);
    ch = fuse_session_next_chan (se, NULL);
    bufsize = fuse_chan_bufsize (ch);
    buf = alloca (bufsize);

    ret = TRUE;
    res = fuse_chan_recv (&ch, buf, bufsize);

    if (res == -EINTR)
        ret = TRUE;
    else if (res <= 0)
        ret = FALSE;
    else
        fuse_session_process (se, buf, res, ch);

    return ret;
}

static void* internal_init_wrapper (struct fuse_conn_info *conn)
{
    struct fuse_context *con;
    PrivateDataBlock *data;

    con = fuse_get_context ();
    data = (PrivateDataBlock*) con->private_data;

    if (data->loop->priv->real_ops->init != NULL)
        data->user = data->loop->priv->real_ops->init (conn);

    return data;
}

/**
 * gfuse_loop_run:
 * @loop: the #GFuseLoop to run
 *
 * Runs a #GFuseLoop, mounting it and adding polling of the FUSE channel in
 * the mainloop. If multi-thread is required, also allocates all working
 * threads
 */
void gfuse_loop_run (GFuseLoop *loop)
{
    int thread;
    struct fuse_session *se;
    struct fuse_chan *ch;
    struct fuse *fuse_session;

    if (loop->priv->real_ops == NULL) {
        g_warning ("Invalid initialization of GFuseLoop, no operations loaded");
        return;
    }

    loop->priv->runtime_data = g_new0 (PrivateDataBlock, 1);
    loop->priv->runtime_data->loop = loop;

    loop->priv->shadow_ops = g_new0 (struct fuse_operations, 1);
    memcpy (loop->priv->shadow_ops, loop->priv->real_ops, sizeof (struct fuse_operations));
    loop->priv->shadow_ops->init = internal_init_wrapper;

    fuse_session = fuse_setup (loop->priv->startup_argc, loop->priv->startup_argv,
                               loop->priv->shadow_ops, sizeof (struct fuse_operations),
                               &loop->priv->mountpoint, &thread, loop->priv->runtime_data);

    loop->priv->threads = (thread != 0);
    se = fuse_get_session (fuse_session);
    ch = fuse_session_next_chan (se, NULL);
    loop->priv->fuse_fd = g_io_channel_unix_new (fuse_chan_fd (ch));

    /**
        TODO    Provide implementation also for multi-threads
    */

    /*
    if (thread)
        g_io_add_watch (loop->priv->fuse_fd, G_IO_IN, manage_fuse_mt, fuse_session);
    else
        g_io_add_watch (loop->priv->fuse_fd, G_IO_IN, manage_fuse_st, fuse_session);
    */

    g_io_add_watch (loop->priv->fuse_fd, G_IO_IN, manage_fuse_st, fuse_session);
}

/**
 * gfuse_loop_get_current:
 *
 * If invoked while into one of the callback implementing different FUSE
 * actions, permits to access the current #GFuseLoop instance from which the
 * function has been executed.
 * Be aware this mechanism uses the native "private_data" area exposed by
 * FUSE himself: when running gfuse_loop_run() the original init() callback
 * is temporary substituted with a custom one which hijack his return value
 * and pack together that effective data and the #GFuseLoop instance. To
 * access your own data, use gfuse_loop_get_private()
 *
 * Return value: the current instance of FUSE mainloop, or NULL if the
 * function is called outside the loop
 */
GFuseLoop* gfuse_loop_get_current ()
{
    struct fuse_context *con;
    PrivateDataBlock *data;

    con = fuse_get_context ();
    if (con == NULL)
        return NULL;

    data = (PrivateDataBlock*) con->private_data;
    return data->loop;
}

/**
 * gfuse_loop_get_mountpoint:
 * @loop: a #GFuseLoop
 *
 * To retrieve the current mountpoint for a running #GFuseLoop
 *
 * Return value: the current mountpoint of the FUSE filesystem
 */
const gchar* gfuse_loop_get_mountpoint (GFuseLoop *loop)
{
    return (const gchar*) loop->priv->mountpoint;
}

/**
 * gfuse_loop_get_private:
 * @loop: a #GFuseLoop
 *
 * To be used to access user's private data from a FUSE callback, has to be
 * used instead of fuse_get_context()->private_data . Look at
 * gfuse_loop_get_current() for details
 *
 * Return value: the private data generated in the init() FUSE's operation
 */
void* gfuse_loop_get_private (GFuseLoop *loop)
{
    struct fuse_context *con;
    PrivateDataBlock *data;

    con = fuse_get_context ();
    data = (PrivateDataBlock*) con->private_data;
    return data->user;
}
