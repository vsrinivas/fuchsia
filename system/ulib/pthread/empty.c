// POSIX historically requires that programs using pthreads should
// link in libpthread in addition to libc. It's still conforming to put all
// of that functionality in libc and provide an empty library so -lpthread
// works as normal without modifying the linker driver. Therefore we
// provide this to ease porting existing programs.
