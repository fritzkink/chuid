/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* includes we need */

#include "../config.h"

#ifndef _WIN32
#include <pwd.h>
#include <grp.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <libgen.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <utime.h>
#include <limits.h>
#else
#include <windows.h>
#include <lm.h>
#include <tchar.h>
#include <conio.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <search.h>
#include <string.h>
#include <signal.h>
#include <stddef.h>
#include <locale.h>
#include <ctype.h>
#ifdef __SunOS_5_10
#include <umem.h>
#endif

#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#endif
/* define some cons */
#define ONEDAY 86400

#ifndef SMAX
#define	SMAX 50
#endif
#define INIT_MODULE 100
#define INIT_TAB_SIZE 70
#define ERR_BUF_LENGTH 1024

#define ERROR 2
#define WARNING 1
#define INFO 0

#define OPENFILES_OFFSET 5
#ifndef PTHREAD_THREADS_MAX
#define PTHREAD_THREADS_MAX 256
#endif

#ifndef NAME_MAX
#define NAME_MAX PATH_MAX
#endif

#ifndef _PATH_DEVNULL
#define _PATH_DEVNULL "/dev/null"
#endif

#define	NEWP(type, num)		(type *) malloc((num) * sizeof(type))
#define	RENEWP(old, type, num)	(type *) realloc((old), (num) * sizeof(type))

struct statistic_counters {
        unsigned long        filecounter;
        unsigned long        dircounter;
        unsigned long        otherscounter;
        unsigned long        linkcounter;
};

typedef struct fs_root {
    char                *dirpath;
    struct fs_root      *next;
} fs_root_t;

typedef struct uidexchange {
    uid_t		  olduid;
    uid_t		  newuid;
    struct uidexchange    *next;
} uidexchange_t;

typedef struct gidexchange {
    gid_t		  oldgid;
    gid_t		  newgid;
    struct gidexchange    *next;
} gidexchange_t;

typedef struct queue_element {
	char			*name;
	long int		dirpos;
        long                    directsubdirs;
	fs_root_t		*fs;
	struct queue_element    *parent;
	struct queue_element    *next;
} queue_element_t;

typedef struct queue_anchor {
    long            element_counter;
    double          speed;
    queue_element_t *last;
    queue_element_t *first;
} queue_anchor_t;

struct h_ent {
   ino_t        ino;
   dev_t        dev;
   struct h_ent *c_link;
};

struct htab {
   unsigned int mod;
   unsigned int het_tab_size;
   unsigned int first_free;
   struct h_ent *het_tab;
   struct h_ent *hash[1];
};

void parsefilelist(const char *file_list_file_name);
void parseexfilelist(const char *exfile_list_file_name);
void parseuidlist(const char *uid_list_file_name);
void h_init(const unsigned int mod, const unsigned int het_tab_size);
short int h_mins(const ino_t , const dev_t);
queue_anchor_t *deq_init(void);
queue_element_t *deq_get(queue_anchor_t *anchor);
void deq_put(queue_anchor_t *anchor, queue_element_t *new);
void deq_push(queue_anchor_t *anchor, queue_element_t *new);
void deq_append(queue_anchor_t *global, queue_anchor_t *local);
void deq_prepend(queue_anchor_t *global, queue_anchor_t *local);
long get_max_openfiles(void);
size_t get_pwd_buffer_size(void);
size_t get_grp_buffer_size(void);
void safe_descriptor(void);
unsigned char *mk_esc_seq(char *string, unsigned char escchar, unsigned char *ret_string);
void print_error(const int severity, const char *emsg);
