/*
 * This program is free software: you can redistribute it and/or dryrun
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
 *
 *
 * Chuid 1.0 Design Documentation
 *
 * Chuid is a tool for fast, parallel change of UID's/GID's according to a provided list of 2-tuples.
 *
 * (1) Input Specification
 * - a set of filesystems
 * - a list of 2-tuples for old uid's and gid's and corresponding new values
 * - the number of threads to be created
 * - the busy-percentage threshold given as a number between 0 and 1
 *
 *
 * (2) Result Specification
 *
 * For each regular file, directory or link UID and GID are checked against the list of 2-tuples
 * provided by the uidlist file. If there is a match with one of the entries the UID/GID will
 * be changed with the corresponding new UID/GID.
 *
 *
 * (3) Algorithm
 *
 * Central assumption underlying the algorithm is that the set of all reporting tree tops together
 * with their result information fits into main memory. (For our analysis runs, this is no real
 * restriction: With a reporting level of 2 and about 150,000 reporting directories all in all, we
 * need about 1GB of main memory for the whole analysis.)
 *
 * Our multi-threaded algorithm is based on the following two principles:
 * (1)	As file systems are by and large tree structures, scans of different subtrees are
 * independent from each other: Synchronization is necessary only in case of files
 * referenced by hardlinks.
 * (2)	Threads need to dispatch work only when too many of them are idle.
 *
 * Therefore, threads should be synchronized only in these two cases.
 * In particular, results in a reporting directory will be accumulated per thread; thus,
 * synchronization for result recording is not needed during the scan.
 *
 *
 * Design
 *
 * Scan Phase:
 *
 * -	Constant, configurable number of threads: no dynamic thread creation or deletion
 * during the scan phase.
 *
 * -	Configurable threshold for the ratio of busy threads vs. total number of threads: If the
 * actual percentage of busy threads is lower than this threshold, the algorithm assumes
 * that too many threads are idle.
 *
 * -	Threads synchronize at a global, mutex-ed stack holding subtree roots to be processed.
 * This global stack is initialized with the roots of all file systems given as input to the
 * algorithm.
 * Actually, there are two global stacks to differentiate between fast and slow file sources;
 * we describe their design below, at the end of the scan phase. Till then, the
 * simplification of one global stack makes it easier to understand the algorithm.
 *
 * -	Each thread takes one node from the global stack and processes its subtree; if the stack
 * is empty, idle threads do a conditional wait for a nodes-available event.
 *
 * -	Each thread has its own private stack for traversing the subtree of the node it took
 * from the global stack.
 *
 * -	Each node ist checked whether UID and/or GID must be changed. If there is a match
 * UID/GID will be changed according to provided list of 2-tuples. This is always a disjunct
 * process in two steps to keep the freedom of changing UID/GID independently of each other.
 *
 * If the child is a regular file with nlink greater than 1, the thread synchronizes at a
 * global hash table to check whether the file has been seen before: If not, the file is
 * registered in the hash, the mutex is released.
 *
 * If the child is a directory, it is pushed on the private stack of the thread for further
 * processing.
 *
 * Otherwise, only the Others-count field in the thread-specific structure is incremented.
 *
 * -	After each child determination, the thread checks whether there are too many threads
 * idle (no synchronization necessary):
 * If so, it stops processing of the current node and, in case of remaining children to be
 * determined, stores the position of the next child in the node and pushes the node back
 * onto its private stack. After that, the thread checks whether there is more than one
 * node in its private stack and in that case prepends all nodes except for the first one to
 * the global stack. The thread continues with the remaining node in its private stack.
 * Otherwise (i.e., if there are enough threads busy), the thread just continues node
 * processing.
 * We considered applying a configurable check period here: A thread would test for too
 * many idle threads only after the amount of time given by the check period had passed
 * since the last test. We found, however, that testing after each child determination did
 * not impact performance (as no synchronization is needed); therefore, we set it
 * conceptually to 0 and removed it from the design.
 *
 * -	The scan phase is completed when a thread finds the global stack empty and the
 * (mutex-ed) count of busy threads to be 0. In this case, it wakes all other threads by
 * signalling that work is finished.
 *
 * -	Two global stacks
 * We generalize the one-global-stack concept to be able to choose between slow and fast
 * sources: There are two global stacks---the fast stack and the slow stack, each has a
 * speed associated with it 0; the fast stack is initialized with all file-system roots. The
 * idea is that if a thread hands over its private-stack elements,
 * (1)	It calculates its processing speed: The number of directory nodes it has
 * processed since it took the last node from the global stacks divided by the time
 * passed since then.
 * (2)	It determines whether they go to the fast or to the slow stack: If the processing
 * speed from Step (1) is above or equal to the average of the two speeds of the
 * stacks, the elements are prepended to the fast stack otherwise, they are
 * prepended to the slow stack.
 * (3)	The speed of the chosen stack is updated: new_speed = processing_speed
 *
 * Thus, each stack's speed is kept updated by transfer events; these two stack speeds are
 * used for calculating how many elements are to be taken from the fast stack before the
 * next element is removed from the slow stack:
 *
 * (1)	This ratio is represented by a global counter, initialized to 0.
 * (2)	If a thread wants to take a new subtree root from the global stacks, it checks the
 * counter: If the counter has a value different from 0, it decrements it and
 * removes an element from the fast stack. If the fast stack was empty, it
 * recalculates the counter and takes an element from the slow stack (see next
 * step).
 * (3)	If  the counter is 0 and the slow stack is not empty, the thread sets the counter
 * to the ceiling of the ratio between the current speed of the fast stack and the
 * current speed of the slow stack; then, it removes an element from the slow
 * stack. If the slow stack was empty, it takes an element from the fast stack; the
 * counter remains 0.
 *
 *
 * Hans Argenton & Fritz Kink, May 2022
 *
 */

#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS
#endif /* ! _POSIX_PTHREAD_SEMANTICS */

#ifndef _WIN32
#include "chuid.h"

static pthread_mutex_t  thr_print = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  thr_nlink = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  thr_queue = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t  thr_handler = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   queue_empty = PTHREAD_COND_INITIALIZER;
static pthread_t        *threads;
#endif
size_t                  numthr = 20;
#ifdef __linux__
static unsigned long    thr_stat;
#elif __sun
static unsigned int     thr_stat;
#elif _WIN32
static HANDLE           thr_stat;
static DWORD		thr_statID;
#else
static int              thr_stat;
#endif
FILE                    *fplog = NULL;

short int               verbose = 0;
static short int        stack = 1;
static short int        dryrun = 0;
static short int        dual_queue = 1;
static short int        stats = 0;
static struct statistic_counters  *stat_counters;
static unsigned int     interval = 300;
static size_t           busy_count = 0;
static short int        notfinished = 1;
static long             fast_nodes_befor_next_slow_node;
static time_t           start_time;
double                  busythreshold = 0.9;

static queue_anchor_t   *fast_anchor = NULL;
static queue_anchor_t   *slow_anchor = NULL;

size_t                  grplinelen = 0;
size_t                  pwdlinelen = 0;

static char             *prog_name = NULL;

struct htab             *htab = NULL;

int                     fnum = 0;
unsigned int            max_openfiles;
char                    *logdir = NULL;
char                    *dirlist = NULL;
char                    *uidlist = NULL;
char                    *exclude_list = NULL;
static fs_root_t        *fs_list_ptr = NULL;
fs_root_t               *begin_fs_list = NULL;
fs_root_t                *begin_exclude_file = NULL;
uidexchange_t		*begin_uidpair = NULL;
gidexchange_t		*begin_gidpair = NULL;

static void
        usage(void) {

/*
 * Description:
 * Prints short usage text.
 *
 */
    printf("Usage: %s [-h] [-v] [-n] [-o] [-q] [-s <interval>] [-b <busy threshold>] [-t <nuber of threads>] -i <input file> -d <directory file> -e <exclude file> -l <logdir> \n", prog_name);
    printf("Version: %s v%s %s, %s fkink Exp $\n", PACKAGE_NAME, VERSION, __DATE__, __TIME__);
    printf("Changes given uid to a new uid (optionally new gid, too) in a given directory.\n\
            \n\
            -i <input file>     input file containing old-uid new-uid respectively old-gid new-gid\n\
            -d <directory file> file containing root directories where changes should take place\n\
            -e <exclude file>   file containing directories/files to exclude from changes\n\
            -l <logdir>         logdir which will contain log output\n\
            -v                  verbose mode\n\
            -q                  queueing vs. stack version\n\
            -n                  dry run - shows files to be changed\n\
            -o                  one queue version\n\
            -b <busy threshold> busy threshold for working threads out of allowed number of threads (default 0.9)\n\
            -t <# of threads>   number of threads (default 20)\n\
            -s <interval>       print continously statistics every <interval> seconds\n\
            -h                  display this help message\n\
            \n");
    printf("\nReport bugs to <%s>.\n", PACKAGE_BUGREPORT);
    exit(EXIT_FAILURE);
}

void
        print_error(const int severity, const char *emsg) {

/*
 * Description:
 * Writes an error message with a time stamp and severity to the log file.
 *
 * Parameters:
 * emsg:    String which contains an error message
 *
 */
    time_t      log_time;
    char        *errorlevel[3] = { "INFO: ", "WARNING: ", "ERROR: "};
    char        timestr[SMAX];
    struct tm   *ltime;
    
    log_time = time(NULL);
    errno = 0;
    if ((ltime = localtime(&log_time)) == NULL) {
        fprintf(stderr, "ERROR: Problems getting local time: %s\n", strerror(errno));
        exit(EINVAL);
    }
    strftime(timestr, (size_t) SMAX, "%a %b %d %H:%M:%S %Y ", ltime);
    errno = 0;
    if (fprintf(fplog, "%s", timestr) < 0) {
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
        exit(ENOSPC);
    }
    errno = 0;
    if (fprintf(fplog, "%s", errorlevel[severity]) < 0) {
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
        exit(ENOSPC);
    }
    errno = 0;
    if (fprintf(fplog, "%s", emsg) < 0) {
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
        exit(ENOSPC);
    }
    errno = 0;
    if (fprintf(fplog, "\n") < 0) {
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
        exit(ENOSPC);
    }
}

static void
        print_error_r(const int severity, const char *emsg) {

/*
 * Description:
 * Writes an error message with a time stamp and severity to the log file in a thread safe way.
 *
 * Parameters:
 * emsg:    String which contains an error message
 *
 */
    struct tm   tim;
    time_t      log_time;
    char        *errorlevel[3] = { "INFO: ", "WARNING: ", "ERROR: "};
    char        timestr[SMAX];
    char        *error_str = NULL;
    
    log_time = time(NULL);
    localtime_r(&log_time, &tim);
    strftime(timestr, (size_t) SMAX, "%a %b %d %H:%M:%S %Y ", &tim);
    pthread_mutex_lock(&thr_print);
    errno = 0;
    if (fprintf(fplog, "%s", timestr) < 0) {
#ifdef HAVE_STRERROR_R
        if((error_str = (char *) malloc(sizeof(char) * (ERR_BUF_LENGTH+1))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for Error string!\n");
            exit(ENOMEM);
        }
        if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0 )
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, error_str);
        else
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: couldn't get error string\n", logdir);
        free(error_str);
#else
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
#endif
        exit(ENOSPC);
    }
    errno = 0;
    if (fprintf(fplog, "%s", errorlevel[severity]) < 0) {
#ifdef HAVE_STRERROR_R
        if((error_str = (char *) malloc(sizeof(char) * (size_t) (ERR_BUF_LENGTH+1))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for Error string!\n");
            exit(ENOMEM);
        }
        if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0 )
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, error_str);
        else
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: couldn't get error string\n", logdir);
        free(error_str);
#else
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
#endif
        exit(ENOSPC);
    }
    errno = 0;
    if (fprintf(fplog, "%s", emsg) < 0) {
#ifdef HAVE_STRERROR_R
        if((error_str = (char *) malloc(sizeof(char) * (size_t) (ERR_BUF_LENGTH+1))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for Error string!\n");
            exit(ENOMEM);
        }
        if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0 )
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, error_str);
        else
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: couldn't get error string\n", logdir);
        free(error_str);
#else
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
#endif
        exit(ENOSPC);
    }
    errno = 0;
    if (fprintf(fplog, "\n") < 0) {
#ifdef HAVE_STRERROR_R
        if((error_str = (char *) malloc(sizeof(char) * (size_t) (ERR_BUF_LENGTH+1))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for Error string!\n");
            exit(ENOMEM);
        }
        if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0 )
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, error_str);
        else
            fprintf(stderr, "ERROR: Problems writing logfile <%s>: couldn't get error string\n", logdir);
        free(error_str);
#else
        fprintf(stderr, "ERROR: Problems writing logfile <%s>: %s\n", logdir, strerror(errno));
#endif
        exit(ENOSPC);
    }
    pthread_mutex_unlock(&thr_print);
}

static void
        delete_fs_list(void) {

/*
 * Description:
 * Deletes and frees the memory of the file system list which was worked on.
 *
 */
    fs_root_t       *ptr = NULL, *ptr1 = NULL;
    
    ptr = begin_fs_list;
    while(ptr != NULL) {
        ptr1 = ptr->next;
        free(ptr->dirpath);
        free(ptr);
        ptr = ptr1;
    }
    begin_fs_list = NULL;
    if (verbose)
        fprintf(stdout, "INFO: File system list successfully deleted!!\n");
}

static void
        delete_ex_list(void) {

/*
 * Description:
 * Deletes and frees the memory of the exclude file/directory list.
 *
 */
    fs_root_t *ptr = NULL, *ptr1 = NULL;

        ptr=begin_exclude_file;
        while(ptr != NULL) {
            ptr1=ptr->next;
            free(ptr->dirpath);
            free(ptr);
            ptr=ptr1;
        }
        begin_exclude_file = NULL;
        if (verbose)
            fprintf(stdout, "INFO: Exclude file/directory list successfully deleted!!\n");
}

static char *
	uidname(const struct stat *stb, char *pwdbuffer) {

/*
 * Description:
 * Returns the name of the uid.
 *
 * Parameters:
 * stb:             data returned by the lstat function
 * pwdbuffer:       buffer provided for the getpwuid_r function needed to be thread safe
 *
 */

    char            *msg = NULL, *error_str = NULL;
    char	    *name = NULL;
#ifndef _WIN32
    int             return_code = 0;
    struct passwd   pwd;
    struct passwd   *pwdptr = &pwd;
    struct passwd   *tempPwdPtr = NULL;
#else
    NET_API_STATUS  return_code;
    DWORD           dwLevel = 10;
    LPUSER_INFO_10  pwd = NULL;
#endif

#ifndef _WIN32
    return_code = getpwuid_r(stb->st_uid, pwdptr, pwdbuffer, pwdlinelen, &tempPwdPtr);
    if ((tempPwdPtr != NULL) && (return_code == 0)) {
        if ((name = strdup(tempPwdPtr->pw_name)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for UID name\n");
            exit(ENOMEM);
        }
    } else if ((tempPwdPtr == NULL) && (return_code == 0)) {
        if ((name = (char *) malloc(sizeof(char) * 8)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for UID name\n");
            exit(ENOMEM);
        }
        snprintf(name, (size_t) 8, "%7d", stb->st_uid);
    } else {
        if ((name = (char *) malloc(sizeof(char) * 3)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for UID name\n");
            exit(ENOMEM);
        }
        strncpy(name, "-1\0", (size_t) 3);
#ifdef HAVE_STRERROR_R
        if((error_str = (char *) malloc(sizeof(char) * (size_t) (ERR_BUF_LENGTH+1))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for Error string!\n");
            exit(ENOMEM);
        }
        if (strerror_r(return_code, error_str, (size_t) ERR_BUF_LENGTH) == 0 ) {
            if ((msg = (char *) malloc(sizeof(char) * (strlen(error_str)+21))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for message string\n");
                exit(ENOMEM);
            }
            snprintf(msg, strlen(error_str)+21, "getpwuid_r failed: %s", error_str);
            print_error_r(WARNING, msg);
            free(msg);
        } else
            print_error_r(WARNING, "getpwuid_r failed: couldn't get error string");
        free(error_str);
#else
        if ((msg = (char *) malloc(sizeof(char) * (strlen(strerror(return_code))+21))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for UID name error string\n");
            exit(ENOMEM);
        }
        snprintf(msg, strlen(strerror(return_code))+21, "getpwuid_r failed: %s", strerror(return_code));
        print_error_r(WARNING, msg);
        free(msg);
#endif
    }
#else
    return_code = NetUserGetInfo("servername", stb, dwLevel, (LPBYTE *)&pwd);
    if (pwd != NULL && return_code == NERR_Success) {
       if ((name = strdup(pwd->usri10_name)) == NULL) {
               fprintf(stderr, "ERROR: No memory available for UID string\n");
               exit(ENOMEM);
        }
    } else {
        if ((msg = (char *) malloc(sizeof(char) * (strlen(strerror(return_code))+30))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for UID error string\n");
            exit(ENOMEM);
        }
        snprintf(msg, strlen(strerror(return_code))+21, "NetUserGetInfo failed: %s", strerror(return_code));
        print_error_r(WARNING, msg);
        free(msg);
    }
#endif
    return name;
}
 
static char *
        gidname(const struct stat *stb, char *grpbuffer) {

/*
 * Description:
 * Returns the name of the gid.
 *
 * Parameters:
 * grpbuffer:       buffer provided for the getgrgid_r function needed to be thread safe
 *
 */

    char            *msg = NULL, *error_str = NULL;
    char            *name = NULL;
#ifndef _WIN32
    int             return_code = 0;
    struct group    grp;
    struct group    *grpptr = &grp;
    struct group    *tempGrpPtr = NULL;
#else
    NET_API_STATUS  return_code;
    DWORD           dwLevel = 10;
    LPUSER_INFO_10  pwd = NULL;
#endif

#ifndef _WIN32
    return_code = getgrgid_r(stb->st_gid, grpptr, grpbuffer, grplinelen, &tempGrpPtr);
    if ((tempGrpPtr != NULL) && (return_code == 0)) {
        if ((name = strdup(tempGrpPtr->gr_name)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for GID name\n");
            exit(ENOMEM);
        }
    } else if ((tempGrpPtr == NULL) && (return_code == 0)) {
        if ((name = (char *) malloc(sizeof(char) * 8)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for GID name\n");
            exit(ENOMEM);
        }
        snprintf(name, (size_t) 8, "%7d", stb->st_gid);
    } else {
        if ((name = (char *) malloc(sizeof(char) * 3)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for GID name\n");
            exit(ENOMEM);
        }
        strncpy(name, "-1\0", (size_t) 3);
#ifdef HAVE_STRERROR_R
        if((error_str = (char *) malloc(sizeof(char) * (size_t) (ERR_BUF_LENGTH+1))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for Error string!\n");
            exit(ENOMEM);
        }
        if (strerror_r(return_code, error_str, (size_t) ERR_BUF_LENGTH) == 0 ) {
            if ((msg = (char *) malloc(sizeof(char) * (strlen(error_str)+21))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for message string\n");
                exit(ENOMEM);
            }
            snprintf(msg, strlen(error_str)+21, "getgrgid_r failed: %s", error_str);
            print_error_r(WARNING, msg);
            free(msg);
        } else
            print_error_r(WARNING, "getgrgid_r failed: couldn't get error string");
        free(error_str);
#else
        if ((msg = (char *) malloc(sizeof(char) * (strlen(strerror(return_code))+30))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for GID name error string\n");
            exit(ENOMEM);
        }
        snprintf(msg, strlen(strerror(return_code))+21, "getgrgid_r failed: %s", strerror(return_code));
        print_error_r(WARNING, msg);
        free(msg);
#endif
    }
#else
    return_code = NetUserGetInfo("servername", stb, dwLevel, (LPBYTE *)&pwd);
    if (pwd != NULL && return_code == NERR_Success) {
       if ((name = strdup(pwd->usri10_name)) == NULL) {
               fprintf(stderr, "ERROR: No memory available for UID string\n");
               exit(ENOMEM);
        }
    } else {
        if ((msg = (char *) malloc(sizeof(char) * (strlen(strerror(return_code))+30))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for UID error string\n");
            exit(ENOMEM);
        }
        snprintf(msg, strlen(strerror(return_code))+21, "NetUserGetInfo failed: %s", strerror(return_code));
        print_error_r(WARNING, msg);
        free(msg);
    }
#endif

    return name;
}

static void 
        process_tile(const unsigned int tid, queue_element_t *qe, char *pwdbuffer, char *grpbuffer) {
    
/*
 * Description:
 * Receives from function handle_subtree a directory out of one of the global deqs and traverses
 * the subtree of this directory. In case of too many idle threads it hands over all but one of 
 * the roots of the subtrees still to be traversed to one of the global deqs.
 *
 * Parameters:
 * tid:         thread id
 * qe:          queue element representing subtree root
 * pwdbuffer:   buffer provided for the getpwuid_r function needed to be thread safe
 * grpbuffer:   buffer provided for the getgrgid_r function needed to be thread safe
 *
 */
    fs_root_t        *efptr = NULL;
    queue_anchor_t  *p_anchor = NULL;
    queue_element_t *p_element = NULL, *w_element = NULL, *first_deq_element = NULL;
    char            *pathofchild = NULL;	/* dirname */
    char            *oname = NULL, *nname = NULL;
#ifndef _WIN32
    DIR             *dp = NULL;
    struct dirent   *dirp = NULL;
#endif
    struct stat     t_statbuf, n_statbuf;
    struct timeval  t1, t2;
    double          scanrate, delta;
    int             directories_scanned = 0;
    int             include = 0, j = 0;
    size_t          len;
    long            deq_count = 0;
    short int       backtodeq = 0, known_nlink_file = 0;
    char            *msg = NULL;
    char            *error_str = NULL;
    short           too_many_idle_threads = 0;
    uidexchange_t   *uptr = NULL;
    gidexchange_t   *gptr = NULL;
    
    p_anchor = deq_init();
    deq_push(p_anchor, qe);
    gettimeofday(&t1, NULL);
    while (p_anchor->element_counter > 0) {
        too_many_idle_threads = 0;
        backtodeq = 0;
        if (dual_queue)
            directories_scanned++;
        w_element = deq_get(p_anchor);
        errno = 0;
        if ((dp = opendir(w_element->name)) != NULL) {
            if (w_element->dirpos != 0) {
                seekdir(dp, w_element->dirpos);
            }
            errno = 0;
            for (dirp = readdir(dp); dirp != NULL && !too_many_idle_threads; dirp = readdir(dp)) {

                include = 1;
                known_nlink_file = 0;
                if ((strcmp(dirp->d_name, ".") != 0) && (strcmp(dirp->d_name, "..") != 0)) {/* ignore dot and dot-dot */

                    efptr = begin_exclude_file;
                    while (efptr != NULL) {
                        if (strcmp(dirp->d_name, efptr->dirpath) != 0) {
                            efptr = efptr->next;
                        } else {
                            include = 0;
                            break;
                        }
                    }
                    if (include) {

                        len = strlen(w_element->name) + strlen(dirp->d_name) + 2;
                        if ((pathofchild = (char *) malloc(sizeof (char) * len)) == NULL) {
                            fprintf(stderr, "ERROR: No memory available for Name string\n");
                            exit(ENOMEM);
                        }
                        snprintf(pathofchild, len, "%s/%s", w_element->name, dirp->d_name);

                        errno = 0;
                        if (lstat(pathofchild, &t_statbuf) == 0) {
                            if (S_ISREG(t_statbuf.st_mode)) {
                                /* if we have an hardlink count bigger then 1 .... */
                                if (t_statbuf.st_nlink > 1) {
                                    pthread_mutex_lock(&thr_nlink);
                                    /* hmins inserts the inode in the hash table and returns 1 if this file had already been visited and 0 if it is new */
                                    known_nlink_file = h_mins(t_statbuf.st_ino, t_statbuf.st_dev);
                                    pthread_mutex_unlock(&thr_nlink);
                                }
                                if (t_statbuf.st_nlink == 1 || !known_nlink_file) {
                                    if (stats) {
                                        stat_counters[tid].filecounter++;
                                    }
                                    if (dryrun) {
					uptr = begin_uidpair;
					while (uptr != NULL) {
					    if (uptr->olduid == t_statbuf.st_uid) {
						n_statbuf = t_statbuf;
						n_statbuf.st_uid = uptr->newuid;
						oname = uidname(&t_statbuf, pwdbuffer);
						nname = uidname(&n_statbuf, pwdbuffer);
						fprintf(stdout, "%s (FILE): %u (%s), uid will be changed to %u (%s)\n", pathofchild, uptr->olduid, oname, uptr->newuid, nname);	
						free(oname);
						free(nname);
						break;
					    }
					    uptr = uptr->next;
                                    	}
					gptr = begin_gidpair;
					while (gptr != NULL) {
					    if (gptr->oldgid == t_statbuf.st_gid) {
						n_statbuf = t_statbuf;
						n_statbuf.st_gid = gptr->newgid;
						oname = gidname(&t_statbuf, grpbuffer);
						nname = gidname(&n_statbuf, grpbuffer);
						fprintf(stdout, "%s (FILE): %u (%s), gid will be changed to %u (%s)\n", pathofchild, gptr->oldgid, oname, gptr->newgid, nname);	
						free(oname);
						free(nname);
						break;
					    }
					    gptr = gptr->next;
                                    	}
                                    } else {
					uptr = begin_uidpair;
					while (uptr != NULL) {
					    if (uptr->olduid == t_statbuf.st_uid) {
                        			errno = 0;
						if (chown(pathofchild, uptr->newuid, (gid_t)-1) == 0) {
						    n_statbuf = t_statbuf;
						    n_statbuf.st_uid = uptr->newuid;
						    oname = uidname(&t_statbuf, pwdbuffer);
						    nname = uidname(&n_statbuf, pwdbuffer);
						    len = strlen(oname) + strlen(nname) + strlen(pathofchild)+ 64;
						    if ((msg = (char *) malloc(sizeof (char) * len)) == NULL) {
							fprintf(stderr, "ERROR: No memory available for message string\n");
							exit(ENOMEM);
						    }
						    snprintf(msg, (size_t) len, "%s (FILE): %11u (%s), uid will be changed to %11u (%s)", pathofchild, uptr->olduid, oname, uptr->newuid, nname);
						    print_error_r(INFO, msg);
						    free(msg);
						    free(oname);
						    free(nname);
						    break;
						} else {
#ifdef HAVE_STRERROR_R
                            			    if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                			fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                			exit(ENOMEM);
                            			    }
                            			    if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(error_str) + 20))) == NULL) {
                                    			    fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			    exit(ENOMEM);
                                			}
                                			snprintf(msg, strlen(pathofchild) + strlen(error_str) + 20, "couldn't stat <%s>: %s", pathofchild, error_str);
                                			print_error_r(WARNING, msg);
                            			    } else {
                                			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + 44))) == NULL) {
                                    			    fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			    exit(ENOMEM);
                                			}
                                			snprintf(msg, strlen(pathofchild) + 44, "couldn't stat <%s>: couldn't get error string", pathofchild);
                                			print_error_r(WARNING, msg);
                            			    }
                            			    free(error_str);
#else
                            			    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(strerror(errno)) + 20))) == NULL) {
                                			fprintf(stderr, "ERROR: No memory available for message string\n");
                                			exit(ENOMEM);
                            			    }
                            			    snprintf(msg, strlen(pathofchild) + strlen(strerror(errno)) + 20, "couldn't stat <%s>: %s", pathofchild, strerror(errno));
                            			    print_error_r(WARNING, msg);
#endif
                            			    free(msg);
						}
					    }
					    uptr = uptr->next;
                                    	}
					gptr = begin_gidpair;
					while (gptr != NULL) {
					    if (gptr->oldgid == t_statbuf.st_gid) {
                        			errno = 0;
						if (chown(pathofchild, (uid_t)-1, gptr->newgid) == 0) {
						    n_statbuf = t_statbuf;
						    n_statbuf.st_gid = gptr->newgid;
						    oname = gidname(&t_statbuf, grpbuffer);
						    nname = gidname(&n_statbuf, grpbuffer);
						    len = strlen(oname) + strlen(nname) + strlen(pathofchild)+ 64;
						    if ((msg = (char *) malloc(sizeof (char) * len)) == NULL) {
							fprintf(stderr, "ERROR: No memory available for message string\n");
							exit(ENOMEM);
						    }
						    snprintf(msg, (size_t) len, "%s (FILE): %11u (%s), gid will be changed to %11u (%s)", pathofchild, gptr->oldgid, oname, gptr->newgid, nname);
						    print_error_r(INFO, msg);
						    free(msg);
						    free(oname);
						    free(nname);
						    break;
						} else {
#ifdef HAVE_STRERROR_R
                            			    if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                			fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                			exit(ENOMEM);
                            			    }
                            			    if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(error_str) + 20))) == NULL) {
                                    			    fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			    exit(ENOMEM);
                                			}
                                			snprintf(msg, strlen(pathofchild) + strlen(error_str) + 20, "couldn't stat <%s>: %s", pathofchild, error_str);
                                			print_error_r(WARNING, msg);
                            			    } else {
                                			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + 44))) == NULL) {
                                    			    fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			    exit(ENOMEM);
                                			}
                                			snprintf(msg, strlen(pathofchild) + 44, "couldn't stat <%s>: couldn't get error string", pathofchild);
                                			print_error_r(WARNING, msg);
                            			    }
                            			    free(error_str);
#else
                            			    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(strerror(errno)) + 20))) == NULL) {
                                			fprintf(stderr, "ERROR: No memory available for message string\n");
                                			exit(ENOMEM);
                            			    }
                            			    snprintf(msg, strlen(pathofchild) + strlen(strerror(errno)) + 20, "couldn't stat <%s>: %s", pathofchild, strerror(errno));
                            			    print_error_r(WARNING, msg);
#endif
                            			    free(msg);
						}
					    }
					    gptr = gptr->next;
					}	
				    }	
                                }
                                free(pathofchild);
                                pathofchild = NULL;
                            } else if (S_ISLNK(t_statbuf.st_mode)) {
                                if (dryrun) {
				    uptr = begin_uidpair;
				    while (uptr != NULL) {
					if (uptr->olduid == t_statbuf.st_uid) {
					    n_statbuf = t_statbuf;
					    n_statbuf.st_uid = uptr->newuid;
					    oname = uidname(&t_statbuf, pwdbuffer);
					    nname = uidname(&n_statbuf, pwdbuffer);
					    fprintf(stdout, "%s (DIRECTORY): %u (%s), uid will be changed to %u (%s)\n", pathofchild, uptr->olduid, oname, uptr->newuid, nname);	
					    free(oname);
					    free(nname);
					    break;
					}
					uptr = uptr->next;
                                    }
				    gptr = begin_gidpair;
				    while (gptr != NULL) {
					if (gptr->oldgid == t_statbuf.st_gid) {
					    n_statbuf = t_statbuf;
					    n_statbuf.st_gid = gptr->newgid;
					    oname = gidname(&t_statbuf, grpbuffer);
					    nname = gidname(&n_statbuf, grpbuffer);
					    fprintf(stdout, "%s (DIRECTORY): %u (%s), gid will be changed to %u (%s)\n", pathofchild, gptr->oldgid, oname, gptr->newgid, nname);	
					    free(oname);
					    free(nname);
					    break;
					}
					gptr = gptr->next;
                                    }
                                } else {
				    uptr = begin_uidpair;
				    while (uptr != NULL) {
			 		if (uptr->olduid == t_statbuf.st_uid) {
                	   		    errno = 0;
					    if (lchown(pathofchild, uptr->newuid, (gid_t)-1) == 0) {
						n_statbuf = t_statbuf;
						n_statbuf.st_uid = uptr->newuid;
						oname = uidname(&t_statbuf, pwdbuffer);
						nname = uidname(&n_statbuf, pwdbuffer);
						len = strlen(oname) + strlen(nname) + strlen(pathofchild)+ 69;
						if ((msg = (char *) malloc(sizeof (char) * len)) == NULL) {
						    fprintf(stderr, "ERROR: No memory available for message string\n");
						    exit(ENOMEM);
						}
						snprintf(msg, (size_t) len, "%s (DIRECTORY): %11u (%s), uid will be changed to %11u (%s)", pathofchild, uptr->olduid, oname, uptr->newuid, nname);
						print_error_r(INFO, msg);
						free(msg);
						free(oname);
						free(nname);
						break;
					    } else {
#ifdef HAVE_STRERROR_R
                            			if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                		    exit(ENOMEM);
                            			}
                            			if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(error_str) + 20))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + strlen(error_str) + 20, "couldn't stat <%s>: %s", pathofchild, error_str);
                                		    print_error_r(WARNING, msg);
                            			} else {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + 44))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + 44, "couldn't stat <%s>: couldn't get error string", pathofchild);
                                		    print_error_r(WARNING, msg);
                            			}
                            			free(error_str);
#else
                            			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(strerror(errno)) + 20))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for message string\n");
                                		    exit(ENOMEM);
                            			}
                            			snprintf(msg, strlen(pathofchild) + strlen(strerror(errno)) + 20, "couldn't stat <%s>: %s", pathofchild, strerror(errno));
                            			print_error_r(WARNING, msg);
#endif
                            			free(msg);
					    }
					}
					uptr = uptr->next;
                                    }
				    gptr = begin_gidpair;
				    while (gptr != NULL) {
					if (gptr->oldgid == t_statbuf.st_gid) {
                        		    errno = 0;
					    if (lchown(pathofchild, (uid_t)-1, gptr->newgid) == 0) {
						n_statbuf = t_statbuf;
						n_statbuf.st_gid = gptr->newgid;
						oname = gidname(&t_statbuf, grpbuffer);
						nname = gidname(&n_statbuf, grpbuffer);
						len = strlen(oname) + strlen(nname) + strlen(pathofchild)+ 69;
						if ((msg = (char *) malloc(sizeof (char) * len)) == NULL) {
						    fprintf(stderr, "ERROR: No memory available for message string\n");
						    exit(ENOMEM);
						}
						snprintf(msg, (size_t) len, "%s (DIRECTORY): %11u (%s), gid will be changed to %11u (%s)", pathofchild, gptr->oldgid, oname, gptr->newgid, nname);
						print_error_r(INFO, msg);
						free(msg);
						free(oname);
						free(nname);
						break;
					    } else {
#ifdef HAVE_STRERROR_R
                            		        if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                		    exit(ENOMEM);
                            			}
                            			if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(error_str) + 20))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + strlen(error_str) + 20, "couldn't stat <%s>: %s", pathofchild, error_str);
                                		    print_error_r(WARNING, msg);
                            			} else {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + 44))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    		    exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + 44, "couldn't stat <%s>: couldn't get error string", pathofchild);
                                		    print_error_r(WARNING, msg);
                            			}
                            			free(error_str);
#else
                            			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(strerror(errno)) + 20))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for message string\n");
                                		    exit(ENOMEM);
                            			}
                            			snprintf(msg, strlen(pathofchild) + strlen(strerror(errno)) + 20, "couldn't stat <%s>: %s", pathofchild, strerror(errno));
                            			print_error_r(WARNING, msg);
#endif
                            			free(msg);
					    }
					}
					gptr = gptr->next;
                                    }
				}	
                                if (stats)
                                    stat_counters[tid].linkcounter++;
                                free(pathofchild);
                                pathofchild = NULL;
                            } else if (S_ISDIR(t_statbuf.st_mode)) {
                                if (dryrun) {
				    uptr = begin_uidpair;
				    while (uptr != NULL) {
					if (uptr->olduid == t_statbuf.st_uid) {
					    n_statbuf = t_statbuf;
					    n_statbuf.st_uid = uptr->newuid;
					    oname = uidname(&t_statbuf, pwdbuffer);
					    nname = uidname(&n_statbuf, pwdbuffer);
					    fprintf(stdout, "%s (DIRECTORY): %u (%s), uid will be changed to %u (%s)\n", pathofchild, uptr->olduid, oname, uptr->newuid, nname);	
					    free(oname);
					    free(nname);
					    break;
					}
					uptr = uptr->next;
                                    }
				    gptr = begin_gidpair;
				    while (gptr != NULL) {
					if (gptr->oldgid == t_statbuf.st_gid) {
					    n_statbuf = t_statbuf;
					    n_statbuf.st_gid = gptr->newgid;
					    oname = gidname(&t_statbuf, grpbuffer);
					    nname = gidname(&n_statbuf, grpbuffer);
					    fprintf(stdout, "%s (DIRECTORY): %u (%s), gid will be changed to %u (%s)\n", pathofchild, gptr->oldgid, oname, gptr->newgid, nname);	
					    free(oname);
					    free(nname);
					    break;
					}
					gptr = gptr->next;
                                    }
                                } else {
				    uptr = begin_uidpair;
				    while (uptr != NULL) {
			 		if (uptr->olduid == t_statbuf.st_uid) {
                	   		    errno = 0;
					    if (chown(pathofchild, uptr->newuid, (gid_t)-1) == 0) {
						n_statbuf = t_statbuf;
						n_statbuf.st_uid = uptr->newuid;
						oname = uidname(&t_statbuf, pwdbuffer);
						nname = uidname(&n_statbuf, pwdbuffer);
						len = strlen(oname) + strlen(nname) + strlen(pathofchild)+ 69;
						if ((msg = (char *) malloc(sizeof (char) * len)) == NULL) {
						    fprintf(stderr, "ERROR: No memory available for message string\n");
						    exit(ENOMEM);
						}
						snprintf(msg, (size_t) len, "%s (DIRECTORY): %11u (%s), uid will be changed to %11u (%s)", pathofchild, uptr->olduid, oname, uptr->newuid, nname);
						print_error_r(INFO, msg);
						free(msg);
						free(oname);
						free(nname);
						break;
					    } else {
#ifdef HAVE_STRERROR_R
                            			if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                		    exit(ENOMEM);
                            			}
                            			if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(error_str) + 20))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + strlen(error_str) + 20, "couldn't stat <%s>: %s", pathofchild, error_str);
                                		    print_error_r(WARNING, msg);
                            			} else {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + 44))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + 44, "couldn't stat <%s>: couldn't get error string", pathofchild);
                                		    print_error_r(WARNING, msg);
                            			}
                            			free(error_str);
#else
                            			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(strerror(errno)) + 20))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for message string\n");
                                		    exit(ENOMEM);
                            			}
                            			snprintf(msg, strlen(pathofchild) + strlen(strerror(errno)) + 20, "couldn't stat <%s>: %s", pathofchild, strerror(errno));
                            			print_error_r(WARNING, msg);
#endif
                            			free(msg);
						free(oname);
						free(nname);
					    }
					}
					uptr = uptr->next;
                                    }
				    gptr = begin_gidpair;
				    while (gptr != NULL) {
					if (gptr->oldgid == t_statbuf.st_gid) {
                        		    errno = 0;
					    if (chown(pathofchild, (uid_t)-1, gptr->newgid) == 0) {
						n_statbuf = t_statbuf;
						n_statbuf.st_gid = gptr->newgid;
						oname = gidname(&t_statbuf, grpbuffer);
						nname = gidname(&n_statbuf, grpbuffer);
						len = strlen(oname) + strlen(nname) + strlen(pathofchild)+ 69;
						if ((msg = (char *) malloc(sizeof (char) * len)) == NULL) {
						    fprintf(stderr, "ERROR: No memory available for message string\n");
						    exit(ENOMEM);
						}
						snprintf(msg, (size_t) len, "%s (DIRECTORY): %11u (%s), gid will be changed to %11u (%s)", pathofchild, gptr->oldgid, oname, gptr->newgid, nname);
						print_error_r(INFO, msg);
						free(msg);
						free(oname);
						free(nname);
						break;
					    } else {
#ifdef HAVE_STRERROR_R
                            		        if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                		    exit(ENOMEM);
                            			}
                            			if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(error_str) + 20))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    			exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + strlen(error_str) + 20, "couldn't stat <%s>: %s", pathofchild, error_str);
                                		    print_error_r(WARNING, msg);
                            			} else {
                                		    if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + 44))) == NULL) {
                                    			fprintf(stderr, "ERROR: No memory available for message string\n");
                                    		    exit(ENOMEM);
                                		    }
                                		    snprintf(msg, strlen(pathofchild) + 44, "couldn't stat <%s>: couldn't get error string", pathofchild);
                                		    print_error_r(WARNING, msg);
                            			}
                            			free(error_str);
#else
                            			if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(strerror(errno)) + 20))) == NULL) {
                                		    fprintf(stderr, "ERROR: No memory available for message string\n");
                                		    exit(ENOMEM);
                            			}
                            			snprintf(msg, strlen(pathofchild) + strlen(strerror(errno)) + 20, "couldn't stat <%s>: %s", pathofchild, strerror(errno));
                            			print_error_r(WARNING, msg);
#endif
                            			free(msg);
					    }
					}
					gptr = gptr->next;
                                    }
				}	
                                if (stats)
                                    stat_counters[tid].dircounter++;
                                w_element->directsubdirs++;
/*
 * this child is a directory which has to be investigated as well so we create a new deq element for it
 */
                                if ((p_element = (queue_element_t *) malloc(sizeof (queue_element_t))) != NULL) {
                                    p_element->dirpos = 0;
                                    p_element->fs = w_element->fs;
                                    p_element->directsubdirs = 0;
                                    p_element->parent = w_element;
                                    p_element->name = pathofchild;
                                    pathofchild = NULL;
                                    p_element->next = NULL;
/*
 * new element is put into the thread's private deq
 */
                                    if (stack)
                                        deq_push(p_anchor, p_element);
                                    else
                                        deq_put(p_anchor, p_element);
                                } else {
                                    fprintf(stderr, "Error allocating memory for new queue element!!\n");
                                    exit(ENOMEM);
                                } /* pathofchild needed for element name of directory child therefore no free */
                            } else {
                                if (stats)
                                    stat_counters[tid].otherscounter++;
                                free(pathofchild);
                                pathofchild = NULL;
                            }
                        } else {
#ifdef HAVE_STRERROR_R
                            if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                exit(ENOMEM);
                            }
                            if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(error_str) + 20))) == NULL) {
                                    fprintf(stderr, "ERROR: No memory available for message string\n");
                                    exit(ENOMEM);
                                }
                                snprintf(msg, strlen(pathofchild) + strlen(error_str) + 20, "couldn't stat <%s>: %s", pathofchild, error_str);
                                print_error_r(WARNING, msg);
                            } else {
                                if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + 44))) == NULL) {
                                    fprintf(stderr, "ERROR: No memory available for message string\n");
                                    exit(ENOMEM);
                                }
                                snprintf(msg, strlen(pathofchild) + 44, "couldn't stat <%s>: couldn't get error string", pathofchild);
                                print_error_r(WARNING, msg);
                            }
                            free(error_str);
#else
                            if ((msg = (char *) malloc(sizeof (char) * (strlen(pathofchild) + strlen(strerror(errno)) + 20))) == NULL) {
                                fprintf(stderr, "ERROR: No memory available for message string\n");
                                exit(ENOMEM);
                            }
                            snprintf(msg, strlen(pathofchild) + strlen(strerror(errno)) + 20, "couldn't stat <%s>: %s", pathofchild, strerror(errno));
                            print_error_r(WARNING, msg);
#endif
                            free(msg);
                            free(pathofchild);
                            pathofchild = NULL;
                        }
/*
 * here we check for busy threads
 */
                        if ((double) busy_count / (double) numthr < busythreshold) {
/*
 * too few threads working so stop processeing of the current node's children.
 */
                            too_many_idle_threads = 1;
                            w_element->dirpos = telldir(dp);
                            errno = 0;
                            if ((dirp = readdir(dp)) != NULL) {
                                deq_put(p_anchor, w_element);
                                backtodeq = 1;
                            }
                            if (errno != 0) {
#ifdef HAVE_STRERROR_R
                                if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                                    fprintf(stderr, "ERROR: No memory available for Error string!\n");
                                    exit(ENOMEM);
                                }
                                if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                                    if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(error_str) + 53))) == NULL) {
                                        fprintf(stderr, "ERROR: No memory available for message string\n");
                                        exit(ENOMEM);
                                    }
                                    snprintf(msg, strlen(w_element->name) + strlen(error_str) + 53, "readdir() at dirpos check failed for directory <%s>: %s", w_element->name, error_str);
                                    print_error_r(WARNING, msg);
                                } else {
                                    if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + 79))) == NULL) {
                                        fprintf(stderr, "ERROR: No memory available for message string\n");
                                        exit(ENOMEM);
                                    }
                                    snprintf(msg, strlen(w_element->name) + 79, "readdir() at dirpos check failed for directory <%s>: couldn't get error string", w_element->name);
                                    print_error_r(WARNING, msg);
                                }
                                free(error_str);
#else
                                if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(strerror(errno)) + 53))) == NULL) {
                                    fprintf(stderr, "ERROR: No memory available for message string\n");
                                    exit(ENOMEM);
                                }
                                snprintf(msg, strlen(w_element->name) + strlen(strerror(errno)) + 53, "readdir() at dirpos check failed for directory <%s>: %s", w_element->name, strerror(errno));
                                print_error_r(WARNING, msg);
#endif
                                free(msg);
                            }
                        }
                    } /* if included in investigation */
                } /* exclude dot files */
            } /* for (dirp = readdir(dp); dirp != NULL && !too_many_idle_threads;... */

            if (errno != 0) {
#ifdef HAVE_STRERROR_R
                if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for Error string!\n");
                    exit(ENOMEM);
                }
                if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                    if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(error_str) + 37))) == NULL) {
                        fprintf(stderr, "ERROR: No memory available for message string\n");
                        exit(ENOMEM);
                    }
                    snprintf(msg, strlen(w_element->name) + strlen(error_str) + 37, "readdir() failed for directory <%s>: %s", w_element->name, error_str);
                    print_error_r(WARNING, msg);
                } else {
                    if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + 62))) == NULL) {
                        fprintf(stderr, "ERROR: No memory available for message string\n");
                        exit(ENOMEM);
                    }
                    snprintf(msg, strlen(w_element->name) + 62, "readdir() failed for directory <%s>: couldn't get error string", w_element->name);
                    print_error_r(WARNING, msg);
                }
                free(error_str);
#else
                if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(strerror(errno)) + 37))) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for message string\n");
                    exit(ENOMEM);
                }
                snprintf(msg, strlen(w_element->name) + strlen(strerror(errno)) + 37, "readdir() failed for directory <%s>: %s", w_element->name, strerror(errno));
                print_error_r(WARNING, msg);
#endif
                free(msg);
            }

            errno = 0;
            if (closedir(dp) < 0) {
#ifdef HAVE_STRERROR_R
                if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for Error string!\n");
                    exit(ENOMEM);
                }
                if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                    if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(error_str) + 28))) == NULL) {
                        fprintf(stderr, "ERROR: No memory available for message string\n");
                        exit(ENOMEM);
                    }
                    snprintf(msg, strlen(w_element->name) + strlen(error_str) + 28, "can't close directory <%s>: %s", w_element->name, error_str);
                    print_error_r(WARNING, msg);
                } else {
                    if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + 53))) == NULL) {
                        fprintf(stderr, "ERROR: No memory available for message string\n");
                        exit(ENOMEM);
                    }
                    snprintf(msg, strlen(w_element->name) + 53, "can't close directory <%s>: couldn't get error string", w_element->name);
                    print_error_r(WARNING, msg);
                }
                free(error_str);
#else
                if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(strerror(errno)) + 28))) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for message string\n");
                    exit(ENOMEM);
                }
                snprintf(msg, strlen(w_element->name) + strlen(strerror(errno)) + 28, "can't close directory <%s>: %s", w_element->name, strerror(errno));
                print_error_r(WARNING, msg);
#endif
                free(msg);
            }
            if (too_many_idle_threads && p_anchor->element_counter > 1) {
/*
 * to make threads working again transfer the directories in the private deq to the global deq. Keep one for
 * continuing work in this thread.
 */
                gettimeofday(&t2, NULL);
                if ((msg = (char *) malloc(sizeof (char) * 40)) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for message string\n");
                    exit(ENOMEM);
                }
                snprintf(msg, (size_t) 40, "too many idle threads (%3ld) detected!", (long) (numthr - busy_count));
                print_error_r(INFO, msg);
                free(msg);
                deq_count = p_anchor->element_counter;
                if (dual_queue) {
                    delta = (double) (t2.tv_sec - t1.tv_sec) + (double) (t2.tv_usec - t1.tv_usec) / 1000000.;
                    scanrate = (delta > 0) ? directories_scanned / delta : directories_scanned;
                    first_deq_element = deq_get(p_anchor);
                    pthread_mutex_lock(&thr_queue);
                    if (scanrate >= (double) (fast_anchor->speed + slow_anchor->speed) / 2.) {
                        if (stack)
                            deq_prepend(fast_anchor, p_anchor);
                        else
                            deq_append(fast_anchor, p_anchor);
                        fast_anchor->speed = scanrate;
                    } else {
                        if (stack)
                            deq_prepend(slow_anchor, p_anchor);
                        else
                            deq_append(slow_anchor, p_anchor);
                        slow_anchor->speed = scanrate;
                    }
                    pthread_mutex_unlock(&thr_queue);
                } else {
                    pthread_mutex_lock(&thr_queue);
                    if (stack)
                        deq_prepend(fast_anchor, p_anchor);
                    else
                        deq_append(fast_anchor, p_anchor);
                    pthread_mutex_unlock(&thr_queue);
                }
                for (j = 0; j < deq_count - 1; j++)
                    pthread_cond_signal(&queue_empty);
                deq_put(p_anchor, first_deq_element);
/*
 * reset directories_scanned after transfer for next transfer scanrate calculation
 */
                directories_scanned = 0;
            }
        } else { /* could not open directory of node w */
#ifdef HAVE_STRERROR_R
            if ((error_str = (char *) malloc(sizeof (char) * (size_t) (ERR_BUF_LENGTH + 1))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for Error string!\n");
                exit(ENOMEM);
            }
            if (strerror_r(errno, error_str, (size_t) ERR_BUF_LENGTH) == 0) {
                if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(error_str) + 20))) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for message string\n");
                    exit(ENOMEM);
                }
                snprintf(msg, strlen(w_element->name) + strlen(error_str) + 20, "couldn't open <%s>: %s", w_element->name, error_str);
                print_error_r(WARNING, msg);
            } else {
                if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + 44))) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for message string\n");
                    exit(ENOMEM);
                }
                snprintf(msg, strlen(w_element->name) + 44, "couldn't open <%s>: couldn't get error string", w_element->name);
                print_error_r(WARNING, msg);
            }
            free(error_str);
#else
            if ((msg = (char *) malloc(sizeof (char) * (strlen(w_element->name) + strlen(strerror(errno)) + 20))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for message string\n");
                exit(ENOMEM);
            }
            snprintf(msg, strlen(w_element->name) + strlen(strerror(errno)) + 20, "couldn't open <%s>: %s", w_element->name, strerror(errno));
            print_error_r(WARNING, msg);
#endif
            free(msg);
        }

        if (!backtodeq) {
            free(w_element->name);
            //free(w_element);
        }
    }
    free(p_anchor);
    p_anchor = NULL;
    free(w_element);
    w_element = NULL;
}

#ifndef _WIN32
static void *
        statistic(void *bla) {
#else
DWORD WINAPI
	statistic(PVOID bla) {
#endif
/*
 * Description:
 * Gathers and prints out on demand some statistical data.
 *
 * Parameters:
 * bla: dummy argument to make the compiler happy
 */
    unsigned long       ofilecount=0, odircount=0, olinkcount=0, gfilecount=0, glinkcount=0, gdircount=0, glcount=0, gocount=0;
    double              fscanrate = 0, dscanrate = 0, lscanrate = 0;
    size_t              i = 0;

    if (dual_queue)
        fprintf(stdout, "\nThreads busy      files   files/s directories/s links/s elements fast-q Speed slow-q Speed\n\n");
    else
        fprintf(stdout, "\nThreads busy      files   files/s directories/s links/s queue elements\n\n");
    sleep(interval);
    while (notfinished) {
        for (i = 0; i < numthr; i++) {
            gfilecount += stat_counters[i].filecounter;
            gdircount += stat_counters[i].dircounter;
            gocount += stat_counters[i].otherscounter;
            glcount += stat_counters[i].linkcounter;
        }
        fscanrate = (gfilecount - ofilecount) / interval;
        dscanrate = (gdircount - odircount) / interval;
        lscanrate = (glinkcount - olinkcount) / interval;
        ofilecount = gfilecount;
        odircount = gdircount;
        olinkcount = glinkcount;
        if (dual_queue)
            fprintf(stdout, "%7ld %4ld %10ld %7.0f %13.0f %7.0f %15ld %5.1f %6ld %5.1f\n", (long) numthr, (long) busy_count, gfilecount, fscanrate, dscanrate, lscanrate, fast_anchor->element_counter, fast_anchor->speed, slow_anchor->element_counter, slow_anchor->speed);
        else
            fprintf(stdout, "%7ld %4ld %10ld %7.0f %13.0f %7.0f %14ld\n", (long) numthr, (long) busy_count, gfilecount, fscanrate, dscanrate,  lscanrate, fast_anchor->element_counter);
        gfilecount = 0;        
        gdircount = 0;        
        glcount = 0;        
        gocount = 0;        
        sleep(interval);
    }
    fprintf(stdout, "\n");
    pthread_exit(EXIT_SUCCESS);
}

static void *
        handle_subtree(void *id) {

/*
 * Description:
 * Chooses one of the global deq's, takes a queue element out of it and calls process_tile for processing it.
 * After process_tile is finished chooses next available subtree root from global deq.
 *
 * Parameter:
 * id: thread id
 */
    queue_element_t *qe = NULL;
    char            *pwdbuffer = NULL;
    char            *grpbuffer = NULL;
    unsigned int    tid = 0;

    tid = *((unsigned int *) id);
    
    if ((pwdbuffer = (char *) malloc(pwdlinelen)) == NULL) {
        fprintf(stderr, "ERROR: No memory available for passwd buffer\n");
        exit(ENOMEM);
    }
    if ((grpbuffer = (char *) malloc(grplinelen)) == NULL) {
        fprintf(stderr, "ERROR: No memory available for group buffer\n");
        exit(ENOMEM);
    }
    
    while (notfinished) {
        pthread_mutex_lock(&thr_queue);
        while ((fast_anchor->element_counter == 0) && (slow_anchor->element_counter == 0) && (notfinished))
            pthread_cond_wait(&queue_empty, &thr_queue);
        if (notfinished) {
            if (dual_queue) {
                if (fast_nodes_befor_next_slow_node > 0) {
                    if ((qe = deq_get(fast_anchor)) != NULL)
                        fast_nodes_befor_next_slow_node--;
                    else if ((qe = deq_get(slow_anchor)) != NULL)
                        //fast_nodes_befor_next_slow_node = lrintl(ceil (fast_anchor->speed / slow_anchor->speed));
                        fast_nodes_befor_next_slow_node = (long) ceil (fast_anchor->speed / slow_anchor->speed);
                } else { /* fast_nodes_befor_next_slow_node == 0 */
                    if ((qe = deq_get(slow_anchor)) != NULL)
/*
* recalculate fast_nodes_befor_next_slow_node             
*/
                        //fast_nodes_befor_next_slow_node = lrintl(ceil (fast_anchor->speed / slow_anchor->speed));
                        fast_nodes_befor_next_slow_node = (long) ceil (fast_anchor->speed / slow_anchor->speed);
                    else
                        qe = deq_get(fast_anchor);
                }
                if (fast_anchor->element_counter == 0 && slow_anchor->element_counter == 0) {
                    fast_anchor->speed = 0;
                    slow_anchor->speed = 0;
                } else if (fast_anchor->element_counter == 0)
                    fast_anchor->speed = slow_anchor->speed;
                else if (slow_anchor->element_counter == 0)
                    slow_anchor->speed = fast_anchor->speed;
            } else
                qe = deq_get(fast_anchor);

            if (qe != NULL) {
                busy_count++;
                pthread_mutex_unlock(&thr_queue);
                process_tile(tid, qe, pwdbuffer, grpbuffer);                
                pthread_mutex_lock(&thr_queue);
                busy_count--;
               if ((busy_count == 0) && (fast_anchor->element_counter == 0) && (slow_anchor->element_counter == 0)) {
/*
* since no more global deq's elements and we are the only busy thread: the scan phase is finished.
* wakeup all threads and tell them to exit.
*/
                    notfinished = 0;
                    pthread_cond_broadcast(&queue_empty);
                }
                pthread_mutex_unlock(&thr_queue);
            } else {
                pthread_mutex_unlock(&thr_queue);
            }
        } else {
            pthread_mutex_unlock(&thr_queue);
        }
    }
    free(pwdbuffer);
    free(grpbuffer);
    pthread_exit(EXIT_SUCCESS);
}

static void
	handler(const int signum) {
/*
 * Description:
 * Signal handler.
 *
 */
    char    *msg = NULL;
    size_t  i;

/*
 *   if threads are already started allow all threads a clean shutdown
*/
    pthread_mutex_lock(&thr_handler);
    if (notfinished) {
        notfinished = 0;
        pthread_cond_broadcast(&queue_empty);
    }
    pthread_mutex_unlock(&thr_handler);
    for (i = 0; i < numthr; i++)
        pthread_join(threads[i], NULL);
    if (stats)
        pthread_join(thr_stat, NULL);
    if ((msg = (char *) malloc(sizeof(char) * (22+strlen(strsignal(signum))))) == NULL) {
        fprintf(stderr, "ERROR: No memory available for message string\n");
        exit(ENOMEM);
    }

    snprintf(msg, 22+strlen(strsignal(signum)), "OOOOPs got Signal <%s>", strsignal(signum));
    print_error(INFO, msg);

    fprintf(stderr, "\n%s\n", msg);
    free(msg);
    free(htab->het_tab);
    free(htab);
    free(threads);
    if (stats)
        free(stat_counters);
    free(fast_anchor);
    fast_anchor = NULL;
    free(slow_anchor);
    slow_anchor = NULL;
    delete_fs_list();
    delete_ex_list();
    fclose(fplog);
    exit(EXIT_FAILURE);
}

int
#ifndef _WIN32
        main(int argc, char **argv) {
#else
        main(int argc, wchar_t **argv) {
#endif

    queue_element_t *element = NULL;
    size_t          i = 0, buflen = 0;
    char            *msg = NULL;
    char            *flog = NULL;
   
    /* OPTIONS and USAGE */
    int             c, u = 0;	/* index -- getopt_long */
    extern char     *optarg;
    extern int      optopt;
#ifndef _WIN32
    size_t          *taskids = NULL;
    struct stat     statbuf;
#else
    HANDLE          taskids;
    DWORD           dwLevel = 10;
    LPUSER_INFO_10  pwd = NULL;
    NET_API_STATUS  return_code;
#endif

    begin_exclude_file = NULL;
    
    /* LIMITS */
    struct rlimit   limits;
    
    struct sigaction vec;
    
    setlocale(LC_ALL, "C");
    
    safe_descriptor();

    max_openfiles = (unsigned int) get_max_openfiles();

    memset (&vec, 0, sizeof(struct sigaction));
    vec.sa_handler = handler;
    sigemptyset(&vec.sa_mask);
    vec.sa_flags = SA_SIGINFO|SA_RESTART;
    
    errno = 0;
    if (sigaction(SIGQUIT, &vec, NULL) == -1)
        perror("Signal handler for Signal QUIT didn't start properly");
    errno = 0;
    if (sigaction(SIGINT, &vec, NULL) == -1)
        perror("Signal handler for Ctrl-C didn't start properly");
    errno = 0;
    if (sigaction(SIGTERM, &vec, NULL) == -1)
        perror("Signal handler for Signal TERM didn't start properly");
    
    prog_name = argv[0];
    
    /* set the current number of file descriptors to max shell value */
    limits.rlim_cur = RLIM_INFINITY;
    limits.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_NOFILE, &limits);
        
    while ((c = getopt(argc, argv, ":hi:d:e:qvnob:s:t:l:")) != -1) {
        switch (c) {
            case 'h':
                usage();
                 break;
           case 'i':
                uidlist = strdup(optarg);
                break;
           case 'd':
                dirlist = strdup(optarg);
                break;
           case 'e':
                exclude_list = strdup(optarg);
                break;
            case 'q':
                stack = 0;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'n':
                dryrun = 1;
                break;
            case 'o':
                dual_queue = 0;
                break;
            case 'b':
                sscanf(optarg, "%lf", &busythreshold);
                stats = 1;
                break;
            case 's':
                sscanf(optarg, "%u", &interval);
                stats = 1;
                break;
            case 't':
                sscanf(optarg, "%lu", &numthr);
		errno = 0;
		if (numthr > PTHREAD_THREADS_MAX) {
		    fprintf(stderr,"ERROR: Number of threads: %ld, allowed Number range: 1 >= # <= %d\n", (long) numthr, PTHREAD_THREADS_MAX);
		    exit(EXIT_FAILURE);
		} else if (max_openfiles - numthr < OPENFILES_OFFSET) {
		    numthr = max_openfiles - OPENFILES_OFFSET;
		    if (verbose)
			fprintf(stdout, "INFO: Due to file descriptor limit # of threads decreased to %ld!\n", (long) numthr);
		}
                break;
           case 'l':
                logdir = strdup(optarg);
                break;
            case ':':
                fprintf(stderr, "\nOption -%c requires an operand!\n\n", optopt);
                break;
            case '?':
                fprintf(stderr, "\nOption -%c unknown!\n\n", optopt);
                u++;
                break;
            default:
                usage();
        }
    }
    
    if (uidlist == NULL) {
        fprintf(stderr, "\nNo uid list file given!\n\n");
        usage();
    }
    
    start_time = time(NULL);
    
    if (logdir == NULL) {
        fprintf(stderr, "ERROR: No LogDir specified\n");
        exit(EXIT_FAILURE);
    }
    
    buflen = strlen(logdir) + 11;
    if ((flog = (char *) malloc(sizeof(char) * buflen)) == NULL) {
        fprintf(stderr, "ERROR: No memory available for log file string\n");
        exit(ENOMEM);
    }
    snprintf(flog, buflen, "%s/chuid_log", logdir);
    errno = 0;
    if ((fplog = fopen(flog, "w")) == NULL) {
        fprintf (stderr, "ERROR: Couldn't open log file <%s>: %s\n", flog, strerror(errno));
        exit(errno);
    }

    print_error(INFO, "chuid started");
            
    parseuidlist(uidlist);
    parsefilelist(dirlist);
    parseexfilelist(exclude_list);
   
    free(uidlist);
    uidlist = NULL;
    free(dirlist);
    dirlist = NULL;
    free(exclude_list);
    exclude_list = NULL;
   
    pwdlinelen = get_pwd_buffer_size();
    grplinelen = get_grp_buffer_size();

    h_init(INIT_MODULE, INIT_TAB_SIZE);
    
#ifndef _WIN32
    if ((taskids = calloc(numthr, sizeof(size_t))) == NULL) {
#else
    if ((taskids = calloc(numthr, sizeof(HANDLE))) == NULL) {
#endif
        fprintf(stderr, "ERROR: No memory available for THREADS ID array\n");
        exit(ENOMEM);
    }
#ifndef _WIN32
    if ((threads = calloc(numthr, sizeof(pthread_t))) == NULL) {
        fprintf(stderr, "ERROR: No memory available for THREADS array\n");
        exit(ENOMEM);
    }
#endif
    
    if (verbose) {
        fprintf(stdout, "INFO: Number of Threads = %ld\n", (unsigned long) numthr);
    }
    
    fast_anchor = deq_init();
    slow_anchor = deq_init();

    if (begin_fs_list == NULL) {
        fprintf(stderr, "ERROR: No files systems to work on!\n");
        exit(EXIT_FAILURE);
    }
    fs_list_ptr = begin_fs_list;
    while (fs_list_ptr != NULL) {
        if ((element = (queue_element_t *) malloc(sizeof(queue_element_t))) != NULL) {
/*
* in case there is no file system specific parameter, use the default
*/
            if ((element->name = strdup(fs_list_ptr->dirpath)) == NULL) {
                fprintf(stderr, "ERROR: No memory available for Name string\n");
                exit(ENOMEM);
            }
            errno = 0;
            if (lstat(element->name, &statbuf) == 0) {
                element->directsubdirs = 0;
                element->parent = NULL;
                element->dirpos = 0;
                element->fs = fs_list_ptr;
                element->next = NULL;
                deq_put(fast_anchor, element);
            } else {
                buflen = sizeof(char) * (strlen(element->name) + strlen(strerror(errno)) + 19);
                if ((msg = (char *) malloc(buflen)) == NULL) {
                    fprintf(stderr, "ERROR: No memory available for message string\n");
                    exit(ENOMEM);
                }
                snprintf(msg, buflen, "couldn't stat <%s>: %s", element->name, strerror(errno));
                print_error(WARNING, msg);
                free(msg);
                free(element->name);
                free(element);
            }
        } else {
            fprintf(stderr, "Error allocating memory for new queue element!!\n");
            exit(ENOMEM);
        }
        fs_list_ptr = fs_list_ptr->next;
    }
    if (fast_anchor->first == NULL) {
        fprintf(stderr, "ERROR: No valid files systems to work on!\n");
        exit(EXIT_FAILURE);
    }
   
    if (stats) {
/*
* initialize thread specific statistic counters
*/
        if ((stat_counters = calloc(numthr, sizeof(struct statistic_counters))) == NULL) {
            fprintf(stderr, "ERROR: No memory available for STATISTICS array\n");
            exit(ENOMEM);
        }
        memset(stat_counters, 0, numthr*sizeof(struct statistic_counters));

        errno = 0;
#ifdef _WIN32
		thr_stat = CreateThread(NULL, 0, statistic, NULL, 0, &thr_statID);
		if (thr_stat == NULL) {

#else
        if (pthread_create(&thr_stat, NULL, statistic, NULL) != 0 ) {
#endif
            fprintf(stderr, "Statistic thread did not start!\n");
            exit(errno);
        }
    }
    for (i = 0; i < numthr; i++) {
#ifndef _WIN32
        taskids[i] = i;
        errno = 0;
        if (pthread_create(&threads[i], NULL, handle_subtree, (void *) &(taskids[i])) != 0 ) {
            fprintf(stderr, "Worker thread %ld did not start!\n", (unsigned long) i);
            exit(errno);
#else
        taskids[i] = CreateThread(NULL, 0, handle_subtree, (PVOID) &(taskids[i]), 0, NULL);
        if (taskids[i] == NULL) {
        fprintf(stderr, "Worker thread %ld did not start!\n", (unsigned long) i);
        exit(EXIT_FAILURE);
#endif
        }
    }

    for (i = 0; i < numthr; i++) {
#ifndef _WIN32
        pthread_join(threads[i], NULL);
#else
        if (WaitForSingleObject((int *) taskids[i], INFINITE) == WAIT_OBJECT_0) {
            fprintf(stderr, "Joining worker thread[%ld] failed!\n", taskids[i]);
            exit(EXIT_FAILURE);
        }
#endif
    }
    if (stats) {
#ifndef _WIN32
        pthread_join(thr_stat, NULL);
#else
        if (WaitForSingleObject(thr_stat, INFINITE) == WAIT_OBJECT_0) {
            fprintf(stderr, "Joining statistic thread failed!\n");
            exit(EXIT_FAILURE);
        }
#endif
    }

    delete_fs_list();
    delete_ex_list();
    if (stats) 
        free(stat_counters);
    free(htab->het_tab);
    free(htab);
    free(fast_anchor);
    free(slow_anchor);
    free(taskids);
#ifndef _WIN32
    free(threads);
#endif

    if (verbose) {
/*	if ((msg = (char *) malloc(sizeof(char) * 23)) == NULL) {
	    fprintf(stderr, "ERROR: No memory available for message string\n");
	    exit(ENOMEM);
	}
	snprintf(msg, (size_t) 23, "Scanrate%6ld files/s", (time(NULL)-start_time > 0) ? gfilecount/(time(NULL)-start_time) : gfilecount);
	print_error(INFO, msg);
	if ((msg = (char *) realloc(msg, sizeof(char) * 29)) == NULL) {
	    fprintf(stderr, "ERROR: No memory available for message string\n");
	    exit(ENOMEM);
	}
	snprintf(msg, (size_t) 29, "Scanrate%6ld directories/s", (time(NULL)-start_time > 0) ? gdircount/(time(NULL)-start_time) : gdircount);
	print_error(INFO, msg);
 
        fprintf(stdout, "INFO: Number of Threads = %ld\n", (long) numthr); */
        fprintf(stdout, "INFO: Max # of open files per process: %d\n", max_openfiles);
        fprintf(stdout, "INFO: size of queue element: %ld\n", sizeof(queue_element_t));
    }

    print_error(INFO, "Scan successfully completed");
    errno = 0;
    if (fclose(fplog) < 0) {
        fprintf (stderr, "ERROR: Couldn't close <%s>: %s\n", flog, strerror(errno));
        exit(errno);
    }
    free(flog);
    flog = NULL;
    free(logdir);
    logdir = NULL;
    exit(EXIT_SUCCESS);
}
