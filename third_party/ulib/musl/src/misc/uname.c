#include <sys/utsname.h>

int uname(struct utsname* uts) {
    // TODO(kulakowski) At least some of this information should come
    // from some system configuration.
    *uts = (struct utsname){
        .sysname = "Fuchsia",
        .nodename = "fuchsia",
        .release = "",
        .version = "",
        .machine = "",
        .__domainname = "",
    };
    return 0;
}
