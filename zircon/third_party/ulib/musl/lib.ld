/* 
 * The APIs traditionally found in -lm, -ldl, or -lpthread are all implemented
 * in libc.so, so this file just redirects the linker to refer there instead.
 */
INPUT(AS_NEEDED(libc.so))
