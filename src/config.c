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

/* functions to parse input files */
#include "chuid.h"

extern short int            verbose;
extern int                  fnum;
extern size_t               numthr;
extern unsigned int         max_openfiles;
extern char                 *logdir;
extern fs_root_t            *begin_fs_list;
extern double               busythreshold;
extern fs_root_t             *begin_exclude_file;
extern uidexchange_t        *begin_uidpair;
extern gidexchange_t        *begin_gidpair;

static void 
        append (const char *dirpath) {

/*
 * Description:
 * Appends a file system to the file system list.
 *
 * Parameters:
 * dirpath:      path of file system root which has to be scanned
 *
 */
    fs_root_t	*ptr, *ptrtmp;
    
    if (begin_fs_list == NULL) {
        if((begin_fs_list = (fs_root_t *) malloc(sizeof(fs_root_t))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for begin\n");
                exit(ENOMEM);
        }
        if ((begin_fs_list->dirpath = strdup(dirpath)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for dirpath entry\n");
            exit(ENOMEM);
        }
        begin_fs_list->next = NULL;
    } else {
        ptr = begin_fs_list;
        while (ptr != NULL) {
            if (strcmp(ptr->dirpath, dirpath) != 0) {
                ptrtmp = ptr;
                ptr = ptr->next;
            } else {
                fprintf(stderr, "WARNING: Duplicate directory/file name: %s!\n", dirpath);
                return;
            }
        }

        if ((ptrtmp->next = (fs_root_t *) malloc(sizeof(fs_root_t))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for new list element\n");
                exit(ENOMEM);
        }
        ptr = ptrtmp->next;
        if ((ptr->dirpath = strdup(dirpath)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for dirpath entry\n");
            exit(ENOMEM);
        }
        ptr->next = NULL;
    }
}

static void 
        appendex (const char *filename) {

/*
 * Description:
 * Appends a file system to the file system list.
 *
 * Parameters:
 * filename:     path of file system root which has to be exluded
 *
 */
    fs_root_t	*ptr, *ptrtmp;
    
    if (begin_exclude_file == NULL) {
        if((begin_exclude_file = (fs_root_t *) malloc(sizeof(fs_root_t))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for begin\n");
                exit(ENOMEM);
        }
        if ((begin_exclude_file->dirpath = strdup(filename)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for dirpath entry\n");
            exit(ENOMEM);
        }
        begin_exclude_file->next = NULL;
    } else {
        ptr = begin_exclude_file;
        while (ptr != NULL) {
            if (strcmp(ptr->dirpath, filename) != 0) {
                ptrtmp = ptr;
                ptr = ptr->next;
            } else {
                fprintf(stderr, "WARNING: Duplicate directory/file name: %s!\n", filename);
                return;
            }
        }

        if ((ptrtmp->next = (fs_root_t *) malloc(sizeof(fs_root_t))) == NULL) {
                fprintf(stderr, "ERROR: No memory available for new list element\n");
                exit(ENOMEM);
        }
        ptr = ptrtmp->next;
        if ((ptr->dirpath = strdup(filename)) == NULL) {
            fprintf(stderr, "ERROR: No memory available for dirpath entry\n");
            exit(ENOMEM);
        }
        ptr->next = NULL;
    }
}

void
        parsefilelist(const char *file_list_file_name) {

/*
 * Description:
 * Parses the file list file and initializes file list
 *
 * Parameters:
 * file_list_name: name of file list file
 *
 */
    char            *lbuf = NULL; /* line buffer */
    FILE            *fp;
    size_t          max_line;

#ifdef _WIN32    
    max_line = (size_t) 1000;
#else
    max_line = (size_t) sysconf(_SC_LINE_MAX);
#endif

    if (verbose)
        fprintf(stdout, "INFO: file list file = %s\n", file_list_file_name);
    
    errno = 0;
    if ((fp = fopen(file_list_file_name, "r")) == NULL) {
        fprintf(stderr, "ERROR: Can't open file list file %s: %s\n", file_list_file_name, strerror(errno));
        exit(errno);
    }
    
    if ((lbuf = (char *) malloc(sizeof(char) * (max_line+1))) == NULL) {
        fprintf(stderr, "ERROR: No memory available for line buffer\n");
        exit(ENOMEM);
    }
    memset(lbuf, 0, (max_line+1));
    while (fgets(lbuf, (size_t) max_line, fp) != NULL) {
    
        if (strchr("\n#", lbuf[0])) continue;
           
        if (strlen(lbuf) < NAME_MAX) {
	    lbuf[strcspn(lbuf, "\n")] = '\0';
            append(lbuf);
        } else {
	    fprintf(stderr, "ERROR: Directory path <%s> longer than allowed by system!\n", lbuf);
            exit(E2BIG);
        }
    }
    free(lbuf);
    lbuf = NULL;
    
    fclose(fp);
    
    if (verbose) {
        
        fs_root_t	*ptr = begin_fs_list;
	fprintf(stdout, "INFO: List of to be scanned directories\n");
	while (ptr != NULL) {
	    fprintf(stdout, "%s\n", ptr->dirpath);
	    ptr = ptr->next;
	}
    }
}

void
        parseexfilelist(const char *file_list_file_name) {

/*
 * Description:
 * Parses the exfile list file and initializes file list
 *
 * Parameters:
 * file_list_name: name of exfile list file
 *
 */
    char            *lbuf = NULL; /* line buffer */
    FILE            *fp;
    size_t          max_line;

#ifdef _WIN32    
    max_line = (size_t) 1000;
#else
    max_line = (size_t) sysconf(_SC_LINE_MAX);
#endif

    if (verbose)
        fprintf(stdout, "INFO: file list file = %s\n", file_list_file_name);
    
    errno = 0;
    if ((fp = fopen(file_list_file_name, "r")) == NULL) {
        fprintf(stderr, "ERROR: Can't open exclude file list file %s: %s\n", file_list_file_name, strerror(errno));
        exit(errno);
    }
    
    if ((lbuf = (char *) malloc(sizeof(char) * (max_line+1))) == NULL) {
        fprintf(stderr, "ERROR: No memory available for line buffer\n");
        exit(ENOMEM);
    }
    memset(lbuf, 0, (max_line+1));
    while (fgets(lbuf, (size_t) max_line, fp) != NULL) {
    
        if (strchr("\n#", lbuf[0])) continue;
           
        if (strlen(lbuf) < NAME_MAX) {
	    lbuf[strcspn(lbuf, "\n")] = '\0';
            appendex(lbuf);
        } else {
	    fprintf(stderr, "ERROR: Directory path <%s> longer than allowed by system!\n", lbuf);
            exit(E2BIG);
        }
    }
    free(lbuf);
    lbuf = NULL;
    
    fclose(fp);
    
    if (verbose) {
        
        fs_root_t	*ptr = begin_exclude_file;
	fprintf(stdout, "INFO: List of excluded files/directories\n");
	while (ptr != NULL) {
	    fprintf(stdout, "%s\n", ptr->dirpath);
	    ptr = ptr->next;
	}
    }
}

void
	uid_list (uid_t olduid, uid_t newuid) {
/*
 * Description:
 * Creates a mapping list of old uid's and their corresponding new uid
 *
 * Parameters:
 * olduid: old uid
 * newuid: new uid
 *
 */
    uidexchange_t    *ptr, *ptrtmp;


    if (begin_uidpair == NULL ) {
	if ((begin_uidpair = (uidexchange_t *) malloc(sizeof(uidexchange_t))) == NULL) {
	    fprintf(stderr, "ERROR: No memory available for begin of mapping list\n");
	    exit(ENOMEM);
	}
	begin_uidpair->olduid = olduid;
	begin_uidpair->newuid = newuid;
	begin_uidpair->next = NULL;
    } else {
	ptr = begin_uidpair;
	while (ptr != NULL) {
            if (ptr->olduid != olduid) {
                ptrtmp = ptr;
                ptr = ptr->next;
            } else {
                fprintf(stderr, "WARNING: Duplicate old uid: %d!\n", olduid);
                return;
            }
        }
	if ((ptrtmp->next = (uidexchange_t *) malloc(sizeof(uidexchange_t))) == NULL) {
	    fprintf(stderr, "ERROR: No memory available for begin of mapping list\n");
	    exit(ENOMEM);
	}
	ptr = ptrtmp->next;
	ptr->olduid = olduid;
	ptr->newuid = newuid;
	ptr->next = NULL;
    }
}
	
void
	gid_list (gid_t oldgid, gid_t newgid) {
/*
 * Description:
 * Creates a mapping list of old uid's and their corresponding new uid and gid (optional)
 *
 * Parameters:
 * oldgid: old gid
 * newgid: new gid
 *
 */
    gidexchange_t    *ptr, *ptrtmp;


    if (begin_gidpair == NULL ) {
	if ((begin_gidpair = (gidexchange_t *) malloc(sizeof(gidexchange_t))) == NULL) {
	    fprintf(stderr, "ERROR: No memory available for begin of mapping list\n");
	    exit(ENOMEM);
	}
	begin_gidpair->oldgid = oldgid;
	begin_gidpair->newgid = newgid;
	begin_gidpair->next = NULL;
    } else {
	ptr = begin_gidpair;
	while (ptr != NULL) {
            if (ptr->oldgid != oldgid) {
                ptrtmp = ptr;
                ptr = ptr->next;
            } else {
                fprintf(stderr, "WARNING: Duplicate old gid: %d!\n", oldgid);
                return;
            }
        }
	if ((ptrtmp->next = (gidexchange_t *) malloc(sizeof(gidexchange_t))) == NULL) {
	    fprintf(stderr, "ERROR: No memory available for begin of mapping list\n");
	    exit(ENOMEM);
	}
	ptr = ptrtmp->next;
	ptr->oldgid = oldgid;
	ptr->newgid = newgid;
	ptr->next = NULL;
    }
}
	
int
	strcmpi(char *s1, char *s2) {
/*
 * Description:
 * Compares two equally long strings case insensitive
 *
 * Parameters:
 * s1:	string 1
 * s2:	string 2
 *
 */
    int i;
     
    if(strlen(s1) != strlen(s2))
        return -1;
         
    for(i=0; i<strlen(s1); i++){
        if(toupper(s1[i]) != toupper(s2[i]))
            return s1[i]-s2[i];
    }
    return 0;
}

void
        parseuidlist(const char *uid_list_file_name) {

/*
 * Description:
 * Parses the uid mapping list file and initializes uid list
 *
 * Parameters:
 * uid_list_name: name of uid list file
 *
 */
    char            *lbuf = NULL; /* line buffer */
    char	    *tag = NULL;
    uid_t	    oldid = 0;
    uid_t	    newid = 0;
    FILE            *fp;
    size_t          max_line = 0;
    int		    items_read = 0;
    int		    linenumber = 0;
    const int	    tag_length = 2;

#ifdef _WIN32
    max_line = (size_t) 1000;
#else
    max_line = (size_t) sysconf(_SC_LINE_MAX);
#endif

    if (verbose)
        fprintf(stdout, "INFO: uid mapping list file = %s\n", uid_list_file_name);
    
    errno = 0;
    if ((fp = fopen(uid_list_file_name, "r")) == NULL) {
        fprintf(stderr, "ERROR: Can't open uid mapping list file %s: %s\n", uid_list_file_name, strerror(errno));
        exit(errno);
    }
    if ((lbuf = (char *) malloc(sizeof(char) * (max_line+1))) == NULL) {
        fprintf(stderr, "ERROR: No memory available for line buffer\n");
        exit(ENOMEM);
    }
    if ((tag = (char *) malloc(sizeof(char) * (tag_length+1))) == NULL) {
        fprintf(stderr, "ERROR: No memory available for tag buffer\n");
        exit(ENOMEM);
    }
    memset(lbuf, 0, max_line);
    memset(tag, 0, tag_length);
    while (fgets(lbuf, (size_t) max_line, fp) != NULL) {
	linenumber++;
        if (strchr("\n#", lbuf[0])) continue;
	lbuf[strcspn(lbuf, "\n")] = '\0';
	items_read = sscanf(lbuf, "%2[ug:]%u%*[, \t]%u", tag, &oldid, &newid);
	tag[strcspn(tag, ":")] = '\0';
	if (strcmpi(tag, "u") == 0 && items_read == 3) {
	    uid_list(oldid, newid);
	} else if (strcmpi(tag, "g") == 0 && items_read == 3) {
	    gid_list(oldid, newid);
	} else {
	    fprintf(stderr, "ERROR:  Mangled input line\n");
	    fprintf(stderr, "<%s>\t LINE: %d\n", lbuf, linenumber);
	}
    }
   
    if (fp != NULL && fclose(fp) == 0) {
	if (verbose)
	    fprintf(stdout, "INFO: Successfully closed file <%s>.\n", uid_list_file_name);
    } else {
	fprintf(stderr, "ERROR: Failure closing file <%s>.\n", uid_list_file_name);
    }
  
    free(lbuf);
    lbuf = NULL;
    free(tag);
    tag = NULL;

    if (verbose) {
        
        uidexchange_t	*uptr = begin_uidpair;
	fprintf(stdout, "INFO: Old uid, new uid\n");
	while (uptr != NULL) {
	    fprintf(stdout, "%u, %u\n", uptr->olduid, uptr->newuid);
	    uptr = uptr->next;
	}

        gidexchange_t	*gptr = begin_gidpair;
	fprintf(stdout, "INFO: Old gid, new gid\n");
	while (gptr != NULL) {
	    fprintf(stdout, "%u, %u\n", gptr->oldgid, gptr->newgid);
	    gptr = gptr->next;
	}
    }
}
