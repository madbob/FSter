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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>
#include <pwd.h>

#include <glib.h>
#include <libdaemon/daemon.h>

#define AVFS_DO_KEY      34567887

typedef struct {
    long        mtype;
    char        command [500];
} AVFSDoReq;

static void print_help (char *program)
{
    printf ("Usage: %s -u <user> -p <path>\n", program);
    printf ("  -h           Print this help and exit\n");
    printf ("  -k           Print the key for the shared message queue and exit\n");
    printf ("  -f           Force to stay foreground\n");
    printf ("  -u <user>    Specify the user for which execute applications\n");
    printf ("  -p <path>    Mountpoint for AVFS, where chroot applications\n");
    printf ("\n");
}

static uid_t verify_user (char *username)
{
    uid_t ret;
    struct passwd *iter;

    ret = 0;

    while ((iter = getpwent ())) {
        if (strcmp (iter->pw_name, username) == 0) {
            if (iter->pw_uid >= 1000) {
                ret = iter->pw_uid;
            }
            else {
                fprintf (stderr, "The user for running processes must have UID >= 1000\n");
                ret = 0;
            }
        }
    }

    endpwent ();
    return ret;
}

static int init_msg_queue ()
{
    int ret;

    /**
        TODO    Be sure this is the only process able to read from the messages queue
    */

    ret = msgget (AVFS_DO_KEY, 0777 | IPC_CREAT);
    if (ret == -1)
        fprintf (stderr, "Unable to init messages queue: %s\n", strerror (errno));

    return ret;
}

static gboolean handle_request (int queue, uid_t user, char *avfs_path)
{
    int ret;
    gchar **params;
    AVFSDoReq req;

    ret = msgrcv (queue, &req, sizeof (req), 0, 0);
    if (ret == -1)
        return FALSE;

    if (fork () == 0) {
        /**
            TODO    Security issue: child process must not access to the shared messages queue
                    for requests
        */

        if (chroot (avfs_path) == -1) {
            fprintf (stderr, "Unable to chroot the process: %s\n", strerror (errno));
            exit (0);
        }

        if (setuid (user) == -1) {
            fprintf (stderr, "Unable to perform privileges drop: %s\n", strerror (errno));
            exit (0);
        }

        params = g_strsplit (req.command, " ", 0);

        if (execv (params [0], params) == -1)
            fprintf (stderr, "Unable to execute required command: %s\n", strerror (errno));

        exit (0);
    }

    return TRUE;
}

int main (int argc, char **argv)
{
    int queue;
    register int i;
    pid_t pid;
    char* user;
    char *avfs_path;
    gboolean foreground;
    uid_t userid;

    daemon_pid_file_ident = (const char*) daemon_ident_from_argv0 (argv [0]);

    pid = daemon_pid_file_is_running ();
    if (pid >= 0) {
        fprintf (stderr, "Daemon already running on PID file %u\n", pid);
        exit (1);
    }

    if (getuid () != 0) {
        fprintf (stderr, "This process has to be executed with root privileges\n");
        exit (1);
    }

    user = NULL;
    avfs_path = NULL;
    foreground = FALSE;

    for (i = 1; i < argc; i++) {
        if (strcmp (argv [i], "-u") == 0)
            user = argv [++i];

        else if (strcmp (argv [i], "-p") == 0)
            avfs_path = argv [++i];

        else if (strcmp (argv [i], "-k") == 0) {
            printf ("%d\n", AVFS_DO_KEY);
            exit (0);
        }

        else if (strcmp (argv [i], "-h") == 0) {
            print_help (argv [0]);
            exit (0);
        }

        else if (strcmp (argv [i], "-f") == 0)
            foreground = TRUE;

        else {
            print_help (argv [0]);
            exit (0);
        }
    }

    if (user == NULL || avfs_path == NULL) {
        print_help (argv [0]);
        exit (1);
    }

    userid = verify_user (user);
    if (userid == 0)
        exit (1);

    if (foreground == FALSE) {
        daemon_retval_init ();

        pid = daemon_fork ();

        if (pid < 0) {
            fprintf (stderr, "Failed to go background\n");
            daemon_retval_done ();
            exit (1);
        }
        else if (pid > 0) {
            if (daemon_retval_wait (20) < 0) {
                fprintf (stderr, "Could not recieve return value from daemon process: %s\n", strerror(errno));
                exit (1);
            }
            else
                exit (0);
        }
        else {
            if (daemon_close_all (-1) < 0) {
                fprintf (stderr, "Failed to close all file descriptors: %s\n", strerror(errno));
                daemon_retval_send (1);
                exit (1);
            }

            if (daemon_pid_file_create () < 0) {
                fprintf (stderr, "Could not create PID file: %s\n", strerror(errno));
                daemon_retval_send (2);
                exit (1);
            }

            queue = init_msg_queue ();
            if (queue == -1) {
                daemon_retval_send (3);
                exit (1);
            }

            daemon_retval_send (0);
        }
    }

    while (handle_request (queue, userid, avfs_path));

    msgctl (queue, IPC_RMID, NULL);
    daemon_pid_file_remove ();
    exit (0);
}
