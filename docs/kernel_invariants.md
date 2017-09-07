# Magenta Kernel Invariants

On x86, Magenta needs to maintain the following invariants for code running
in ring 0 (kernel mode).

These invariants are documented here because they are not necessarily easy
to test -- breaking an invariant will not necessarily be caught by
Magenta's test suite.

* Flags register:

  * The direction flag (DF) should be 0.  This is required by the x86
    calling conventions.

    If this flag is set to 1, uses of x86 string instructions (e.g. `rep
    movs` in `memcpy()` or inlined by the compiler) can go wrong and copy
    in the wrong direction.  It is OK for a function to set this flag to 1
    temporarily as long as it changes it back to 0 before returning or
    calling other functions.

  * The alignment check flag (AC) should normally be 0.  On CPUs that
    support SMAP, this prevents the kernel from accidentally reading or
    writing userland data.

* The `gs_base` register must point to the current CPU's `x86_percpu`
  struct whenever running in kernel mode with interrupts enabled.
  `gs_base` should only be changed to point to something else while
  interrupts are disabled.  For example, the `swapgs` instruction should
  only be used when interrupts are disabled.

* The following are usually enforced by the compiler:

  * No use of extended registers (SSE, AVX, x87, etc.) is allowed, because
    that would clobber userland's register state.

    This is generally enforced by passing `-mno-sse` to the compiler.  That
    option prevents accidentally using `float` or `double` types in kernel
    code.  It is also necessary to prevent the compiler from using SSE
    registers in optimizations (e.g. memory copies).

  * No storing data below `%rsp` on the stack.  Note that userland code can
    do this: the SysV x86-64 ABI allows functions to store data in the "red
    zone", which is the 128 bytes below %rsp.  However, kernel code cannot
    use the red zone because interrupts may clobber this region -- the CPU
    pushes data onto the stack immediately below %rsp when it invokes an
    interrupt handler.

    This is generally enforced by passing `-mno-red-zone` to the compiler.
