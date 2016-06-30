// POSIX historically requires that programs using math functions should
// link in libm in addition to libc. It's still conforming to put all
// of that functionality in libc and provide an empty library so -lm
// works as normal without modifying the linker driver. Therefore we
// provide this to ease porting existing programs.
