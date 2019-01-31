#define _GNU_SOURCE
#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

int getdomainname(char* name, size_t len) {
    struct utsname temp;
    uname(&temp);
    if (!len || strlen(temp.domainname) >= len) {
        errno = EINVAL;
        return -1;
    }
    strcpy(name, temp.domainname);
    return 0;
}
