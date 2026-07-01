//
// Created by Artur Twardzik on 31/03/2026.
//

#ifndef OS_ERRNO_H
#define OS_ERRNO_H

#define	EPERM 1		/* Not owner */
#define	ENOENT 2	/* No such file or directory */
#define	ESRCH 3		/* No such process */
#define	EINTR 4		/* Interrupted system call */
#define	EIO 5		/* I/O error */
#define	ENXIO 6		/* No such device or address */
#define	E2BIG 7		/* Arg list too long */
#define	ENOEXEC 8	/* Exec format error */
#define	EBADF 9		/* Bad file number */
#define	ECHILD 10	/* No children */
#define	EAGAIN 11	/* No more processes */
#define	ENOMEM 12	/* Not enough space */
#define	EACCES 13	/* Permission denied */
#define	EFAULT 14	/* Bad address */
#define	ENOTBLK 15	/* Block device required */
#define	EBUSY 16	/* Device or resource busy */
#define	EEXIST 17	/* File exists */
#define	EXDEV 18	/* Cross-device link */
#define	ENODEV 19	/* No such device */
#define	ENOTDIR 20	/* Not a directory */
#define	EISDIR 21	/* Is a directory */
#define	EINVAL 22	/* Invalid argument */
#define	ENFILE 23	/* Too many open files in system */
#define	EMFILE 24	/* File descriptor value too large */
#define	ENOTTY 25	/* Not a character device */
#define	ETXTBSY 26	/* Text file busy */
#define	EFBIG 27	/* File too large */
#define	ENOSPC 28	/* No space left on device */
#define	ESPIPE 29	/* Illegal seek */
#define	EROFS 30	/* Read-only file system */
#define	EMLINK 31	/* Too many links */
#define	EPIPE 32	/* Broken pipe */
#define	EDOM 33		/* Mathematics argument out of domain of function */
#define	ERANGE 34	/* Result too large */
#define	ENOMSG 35	/* No message of desired type */
#define	EIDRM 36	/* Identifier removed */

#define ENOSYS 88	/* Function not implemented */

#define ECONNREFUSED 111	/* Connection refused */
#define EADDRINUSE 112		/* Address already in use */
#define ECONNABORTED 113	/* Software caused connection abort */
#define ENETUNREACH 114		/* Network is unreachable */
#define ENETDOWN 115		/* Network interface is not configured */
#define ETIMEDOUT 116		/* Connection timed out */
#define EHOSTDOWN 117		/* Host is down */
#define EHOSTUNREACH 118	/* Host is unreachable */
#define EINPROGRESS 119		/* Connection already in progress */
#define EALREADY 120		/* Socket already connected */
#define EDESTADDRREQ 121	/* Destination address required */
#define EMSGSIZE 122		/* Message too long */
#define EPROTONOSUPPORT 123	/* Unknown protocol */
#define ESOCKTNOSUPPORT 124	/* Socket type not supported */
#define EADDRNOTAVAIL 125	/* Address not available */
#define ENETRESET 126		/* Connection aborted by network */
#define EISCONN 127		/* Socket is already connected */
#define ENOTCONN 128		/* Socket is not connected */
#define ENOSOCKFREE 129         /* No sockets are free */
#define EAFNOSUPPORT 130        /* Address family not supported */

#define MAX_ERRNO 131

#endif //OS_ERRNO_H
