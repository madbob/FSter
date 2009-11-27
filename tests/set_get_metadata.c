#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <attr/xattr.h>

#include <glib.h>
#include <guglielmo-glib.h>

#define METADATA_NAME_LEN       100
#define METADATA_VALUE_LEN      100

static gboolean     verbose        = FALSE;

static void verbose_print (const char *format, ...)
{
    va_list list;

    if (verbose == FALSE)
        return;

    va_start (list, format);
    vprintf (format, list);
    va_end (list);
}

static void now_printf (const char *format, ...)
{
    va_list list;

    va_start (list, format);
    vprintf (format, list);
    fflush (stdout);
    va_end (list);
}

static char** full_listxattr (char *path)
{
    register int i;
    int allocated;
    int ret;
    int index;
    char *names;
    char **to_return;

    allocated = 100;

    while (1) {
        names = alloca (allocated);

        ret = listxattr (path, names, allocated);
        if (ret == -1 && errno == ERANGE) {
            allocated *= 2;
            continue;
        }

        break;
    }

    index = 0;
    allocated = 10;
    to_return = calloc (allocated, sizeof (char*));

    for (i = 0; i < ret; i++) {
        to_return [index] = strdup (names + i);
        i += strlen (to_return [index]);
        index++;

        if (index == allocated) {
            to_return = realloc (to_return, (allocated * 2) * sizeof (char*));
            memset (to_return + allocated, 0, allocated);
            allocated *= 2;
        }
    }

    verbose_print ("there are %d metadata\n", index);

    if (index == allocated)
        to_return = realloc (to_return, (allocated + 1) * sizeof (char*));

    to_return [index] = NULL;
    return to_return;
}

static int random_string (char *string, int size)
{
    register int i;
    int my_len;
    int num_chars;
    char avail_chars [100];

    /*
        I'm too lazy to init this array once in the code...
        C'mon, it's just a unit test!
    */

    num_chars = 0;

    for (i = 48; i <= 57; i++) {
        avail_chars[num_chars] = i;
        num_chars++;
    }
    for (i = 65; i <= 90; i++) {
        avail_chars[num_chars] = i;
        num_chars++;
    }
    for (i = 97; i <= 122; i++) {
        avail_chars[num_chars] = i;
        num_chars++;
    }

    num_chars--;

    srand (time(NULL) + getpid());

    do {
        my_len = rand () % size;
    } while (my_len == 0);

    memset (string, 0, size);

    for (i = 0; i < my_len; i++)
        string[i] = avail_chars[rand() % num_chars];

    return my_len + 1;
}

static int random_metadata_name (char *string, int size)
{
    char *name;

    name = alloca (size);
    random_string (name, size);
    return snprintf (string, size, METADATA_NAME_PREFIX "%s", name);
}

static void print_metadata (char *path, char **metadata)
{
    register int i;
    int ret;
    char value [500];

    printf ("%s:\n", path);

    for (i = 0; metadata[i] != NULL; i++) {
        memset (value, 0, 500);
        ret = getxattr (path, metadata[i], value, 500);
        printf ("\t%s = %s\n", metadata [i], value);
    }

    printf ("\n");
}

static gboolean check_metadata (char *path, char *metadata, gboolean is)
{
    register int i;
    gboolean found;
    char **metadata_list;

    found = FALSE;
    metadata_list = full_listxattr (path);

    for (i = 0; metadata_list[i] != NULL; i++) {
        verbose_print ("comparing %s and %s\n", metadata_list[i], metadata);

        if (strcmp(metadata_list[i], metadata) == 0) {
            found = TRUE;
            break;
        }
    }

    g_strfreev (metadata_list);

    if ((is == TRUE && found == FALSE) || (is == FALSE && found == TRUE)) {
        printf ("list of xattr not consistant, metadata %s %sfound\n", metadata, found == TRUE ? "" : "not ");
        return FALSE;
    }
    else
        return TRUE;
}

static gboolean do_metadata_storm (char *path)
{
    register int i;
    int len;
    int ret;
    char metadata_name [METADATA_NAME_LEN];
    char new_value [METADATA_VALUE_LEN];
    char read_value [METADATA_VALUE_LEN];

    for (i = 0; i < 10; i++) {
        now_printf (".");

        random_metadata_name (metadata_name, METADATA_NAME_LEN);
        len = random_string (new_value, METADATA_VALUE_LEN);

        now_printf ("s");
        ret = setxattr (path, metadata_name, new_value, len, XATTR_CREATE);
        if (ret == -1) {
            printf ("error setxattr(): %s\n", strerror (errno));
            return FALSE;
        }

        now_printf ("g");
        memset (read_value, 0, METADATA_VALUE_LEN);
        ret = getxattr (path, metadata_name, read_value, METADATA_VALUE_LEN);
        if (ret == -1) {
            printf ("error getxattr(): %s\n", strerror (errno));
            return FALSE;
        }

        if (strcmp (read_value, new_value) != 0) {
            printf ("inconsistant xattr: '%s' (%ld) != '%s' (%ld)\n", read_value, strlen (read_value), new_value, strlen (new_value));
            return FALSE;
        }

        if (check_metadata (path, metadata_name, TRUE) == FALSE)
            return FALSE;

        now_printf ("r");
        ret = removexattr (path, metadata_name);
        if (ret == -1) {
            printf ("error removexattr(): %s\n", strerror (errno));
            return FALSE;
        }

        if (check_metadata (path, metadata_name, FALSE) == FALSE)
            return FALSE;
    }

    now_printf ("\n");
    return TRUE;
}

int main (int argc, char **argv)
{
    register int i;
    int num;
    int final_ret;
    gboolean metadata_storm;
    gboolean metadata_storm_error;
    char *home;
    char complete_path[500];
    char **metadata;
    struct dirent **folders;

    metadata_storm = FALSE;
    metadata_storm_error = FALSE;
    final_ret = 0;
    home = getenv ("HOME");
    verbose = FALSE;

    for (i = 0; i < argc; i++) {
        if (strcmp (argv [i], "-s") == 0)
            metadata_storm = TRUE;

        else if (strcmp (argv [i], "-c") == 0)
            home = argv [++i];

        else if (strcmp (argv [i], "-v") == 0)
            verbose = TRUE;
    }

    num = scandir (home, &folders, NULL, alphasort);

    for (i = 0; i < num; i++) {
        if (folders[i]->d_name[0] != '.') {
            snprintf (complete_path, 500, "%s/%s", home, folders[i]->d_name);

            metadata = full_listxattr (complete_path);
            print_metadata (complete_path, metadata);
            g_strfreev (metadata);

            if (metadata_storm == TRUE) {
                metadata_storm = do_metadata_storm (complete_path);
                if (metadata_storm == FALSE) {
                    final_ret = 1;
                    metadata_storm_error = TRUE;
                    break;
                }
            }
        }

        free (folders[i]);
    }

    for (; i < num; i++)
        free (folders[i]);
    free (folders);

    exit (final_ret);
}
