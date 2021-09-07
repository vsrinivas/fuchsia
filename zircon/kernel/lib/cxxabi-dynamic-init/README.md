# Dynamic Initialization Guard Library

This library provides kernel implementations of C++ compiler callouts
for acquiring/releasing static initializer guard variables:
 * __cxa_guard_acquire
 * __cxa_guard_release
 * __cxa_guard_abort

The purpose of implementing these functions is to detect, at runtime,
the use of function scoped static variables.  In development builds
(i.e. builds where ZX_DEBUG_ASSERT_IMPLEMENTED is true), calling one
of these functions after global constructors have executed will result
in a kernel panic.

See also [validate-kernel-symbols.py](//zircon/kernel/scripts/validate-kernel-symbols.py).
