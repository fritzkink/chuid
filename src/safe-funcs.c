#include "chuid.h"

extern unsigned int    max_openfiles;


long
        get_max_openfiles(void) {

/*
 * Description:
 * Returns the maximum allowed open files descriptors per process.
 *
 * Parameter:
 *
 * Return value:
 * filedes: number of max allowed file descriptors
 *
 */
    long filedes;
#ifdef _SC_OPEN_MAX
    errno = 0;
    filedes = sysconf(_SC_OPEN_MAX);
    if ((filedes == -1) && (errno != 0))
        filedes = 1024;
    return filedes;
#endif
}

size_t
        get_pwd_buffer_size(void) {

/*
 * Description:
 * Returns the maximum buffer size needed for getpwuid_r.
 *
 * Parameter:
 *
 * Return value:
 * sz: buffer size
 *
 */
    enum { buf_size = 20480 };  /* default is 20 KB */
    long sz = buf_size;
    
#ifdef _SC_GETPW_R_SIZE_MAX
    errno = 0;
    sz = sysconf(_SC_GETPW_R_SIZE_MAX);
    if ((sz == -1) && (errno != 0))
        sz = buf_size;
#endif

    return (size_t) sz;
}

size_t
        get_grp_buffer_size(void) {

/*
 * Description:
 * Returns the maximum buffer size needed for getgrgid_r.
 *
 * Parameter:
 *
 * Return value:
 * sz: buffer size
 *
 */
    enum { buf_size = 20480 };  /* default is 20 KB */
    long sz = buf_size;
    
#ifdef _SC_GETGR_R_SIZE_MAX
    errno = 0;
    sz = sysconf(_SC_GETGR_R_SIZE_MAX);
    if ((sz == -1) && (errno != 0))
        sz = buf_size;
#endif

#ifdef __linux__
    return (size_t) buf_size;
#else
    return (size_t) sz;
#endif
}
        
static int 
        fd_devnull(const int fd) {

/*
 * Description:
 * Opens standard file descriptors with /dev/null as the case my be.
 *
 * Parameter:
 *
 * Return value:
 * fd: file descriptor
 *
 */
    FILE *fz = NULL;
    switch(fd) {
        case 0:
            fz = freopen(_PATH_DEVNULL, "rb", stdin);
            break;
        case 1:
            fz = freopen(_PATH_DEVNULL, "wb", stdout);
            break;
        case 2:
            fz = freopen(_PATH_DEVNULL, "wb", stderr);
            break;
        default:
            break;
    }
    return (fz && fileno(fz) == fd);
}

void
        safe_descriptor(void) {

/*
 * Description:
 * Make sure that only stdin, stdout and stderr are open.
 *
 */
    int             fd, fdsize;
    struct  stat    st;
    
    if ((fdsize = getdtablesize()) == -1)
        fdsize = (int) max_openfiles;
    for (fd=3; fd < fdsize; fd++)
        close(fd);
    
    for (fd=0; fd < 3; fd++)
        if (fstat(fd, &st) == -1 && (errno != EBADF || !fd_devnull(fd)))
            abort();
}

unsigned char *
        mk_esc_seq(char *string, unsigned char escchar, unsigned char *ret_string) {
/*
 * Description:
 * Generate a string with special nonprintable characters replaced with
 * escape character sequences
 *
 * Parameter:
 * string:          input string
 * escchar:         escape character (should usually be \\ )
 * ret_string:      space for writing the string. If NULL, space is malloced
 *
 * Return value:
 * new_string:      ret_string, if not NULL, otherwise the malloced escaped string
*/
    unsigned char   *new_string, *cptr, c, *strptr, r, obuf[5], *endp;
    size_t          len;

    len = strlen(string);
    strptr = (unsigned char *) string + len;

    if(!ret_string) {
        new_string = NEWP(unsigned char, (len << 2) + 1);
        if(!new_string)
            return(NULL);
    } else
        new_string = ret_string;

    endp = cptr = new_string + (len << 2);
    *cptr = '\0';
    for(; len > 0; len--) {
        c = *(--strptr);
        if(c == '\"' || c == escchar) {
            *(--cptr) = c;
            *(--cptr) = escchar;
        } else {
            if(isprint(c) || ((c & 0x80) && (c & 0x60))) {
                *(--cptr) = c;
            } else {
                r = '\0';

                switch(c) {
                    case '\n':
                        r = 'n';
                        break;
                    case '\t':
                        r = 't';
                        break;
                    case '\a':
                        r = 'a';
                        break;
                    case '\b':
                        r = 'b';
                        break;
                    case '\r':
                        r = 'r';
                        break;
                    case '\f':
                        r = 'f';
                        break;
                    case '\v':
                        r = 'v';
                        break;
                }

                if (r) {
                    *(--cptr) = r;
                    *(--cptr) = escchar;
                } else{
                    cptr -= 2;
                    sprintf((char *) obuf, "%03o", (unsigned int) c);
                    memcpy(--cptr, obuf, 3 * sizeof(unsigned char));
                    *(--cptr) = escchar;
                }
            }
        }
    }

    len = (size_t) (endp - cptr);
    memmove(new_string, cptr, ++len);
    if(!ret_string)
        new_string = RENEWP(new_string, unsigned char, len);

    return(new_string);
}

