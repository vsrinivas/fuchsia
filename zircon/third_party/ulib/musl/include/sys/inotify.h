#ifndef SYSROOT_SYS_INOTIFY_H_
#define SYSROOT_SYS_INOTIFY_H_

#include <fcntl.h>
#include <stdint.h>

struct inotify_event {
  int wd;           // file descriptor referring to the inotify instance whose watch list is to
                    // be modified.
  uint32_t mask;    // Mask describing event.
  uint32_t cookie;  // Unique cookie associating related events (for rename(2)). Not used for now.
  uint32_t len;     // Size of name field.
  char name[];      // Optional null-terminated name.
};

#define IN_CLOEXEC O_CLOEXEC
#define IN_NONBLOCK O_NONBLOCK

// Events to watch in inotify
#define IN_ACCESS 0x00000001         // File was accessed.
#define IN_MODIFY 0x00000002         // File was modified.
#define IN_ATTRIB 0x00000004         // Metadata was changed.
#define IN_CLOSE_WRITE 0x00000008    // Writeable file was closed.
#define IN_CLOSE_NOWRITE 0x00000010  // Unwriteable file closed.
#define IN_OPEN 0x00000020           // File was opened.
#define IN_MOVED_FROM 0x00000040     // File was moved from some location.
#define IN_MOVED_TO 0x00000080       // File was moved to some location.
#define IN_CREATE 0x00000100         // Subfile was created.
#define IN_DELETE 0x00000200         // Subfile was deleted.
#define IN_DELETE_SELF 0x00000400    // Self was deleted.
#define IN_MOVE_SELF 0x00000800      // Self was moved.

#define IN_UNMOUNT 0x00002000     // Backing fs was unmounted.
#define IN_Q_OVERFLOW 0x00004000  // Event queued overflowed.
#define IN_IGNORED 0x00008000     // File was ignored.

// Helper events
#define IN_CLOSE (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#define IN_MOVE (IN_MOVED_FROM | IN_MOVED_TO)

// Special flags
#define IN_ONLYDIR 0x01000000      // only watch the path if it is a directory.
#define IN_DONT_FOLLOW 0x02000000  // don't follow a sym link.
#define IN_EXCL_UNLINK 0x04000000  // exclude events on unlinked objects.
#define IN_MASK_CREATE 0x10000000  // only create watches.
#define IN_MASK_ADD 0x20000000     // add to the mask of an already existing watch.
#define IN_ISDIR 0x40000000        // event occurred against dir.
#define IN_ONESHOT 0x80000000      // only send event once.

#define IN_ALL_EVENTS                                                                \
  (IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_OPEN | \
   IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF)

#ifdef __cplusplus
extern "C" {
#endif

int inotify_init(void);
int inotify_init1(int);
int inotify_add_watch(int, const char*, uint32_t);
int inotify_rm_watch(int, int);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SYSROOT_SYS_INOTIFY_H_
