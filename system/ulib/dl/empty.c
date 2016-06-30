// POSIX historically requires that programs using dlopen etc. should
// link in libdl in addition to libc. It's still conforming to put all
// of that functionality in libc and provide an empty library so -ldl
// works as normal without modifying the linker driver. Therefore we
// provide this to ease porting existing programs.
