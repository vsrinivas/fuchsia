#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "libc.h"

int setlogmask(int maskpri) { return maskpri; }

void closelog(void) {}

void openlog(const char* ident, int opt, int facility) {
  (void)ident;
  (void)opt;
  (void)facility;
}

void __vsyslog(int priority, const char* message, va_list ap) {
  (void)priority;
  (void)message;
  (void)ap;
}

void syslog(int priority, const char* message, ...) {
  (void)priority;
  (void)message;
}

weak_alias(__vsyslog, vsyslog);
