# standalone-test library

This library provides some simple APIs and glue code for test code that is
meant to run exclusively as part of a standalone test executable launched
directly by userboot.  Such tests can't use any normal system services, since
there are no other services running at all.  Instead, they can directly use the
privileged resources and VMOs provided by the kernel.  The
[lib/standalone-test/standalone.h](include/lib/standalone-test/standalone.h)
header provides simple C++ functions in the `standalone` namespace for getting
these handles.

Standalone tests like this need to take care to minimize their library
dependencies, avoiding anything like [fdio] that interacts with other system
services.  Instead, linking in the `standalone-test` provides some simple
replacements for functions like `write` that send any stdout/stderr text lines
as kernel debuglog messages.

[fdio]: /sdk/lib/fdio
