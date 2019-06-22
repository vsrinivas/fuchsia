# Hermetic Compute Engines

This library implements the concept of a "hermetic compute process" that runs a
"hermetic compute engine".  This is a form of "extreme sandboxing" that is
appropriate for code that is heavy on computation and light on communication.

A hermetic compute process is a process that is even more thoroughly isolated
from the rest of the system than Fuchsia processes already are.  It does not
participate in the usual process startup protocols but instead is always set up
very precisely by a controlling process.  A hermetic compute process can be
started with **no handles at all**.  It need not even have access to its own
process or root VMAR handle--and usually doesn't.  The code that runs in a
hermetic compute process must be built specially for this purpose and is not a
normal executable.  We call the special executable a "hermetic compute engine".

The common model will be that the controlling process loads a fresh process
with a hermetic compute engine and maps VMOs into that process to supply input
and output buffers.  The engine's code then performs its computation and exits.
The controlling process notices that the hermetic compute process has exited,
and then collects the output data left in a VMO.

## Launching hermetic compute processes

The **HermeticComputeProcess** class in
[<lib/hermetic-compute/hermetic-compute.h>](include/lib/hermetic-compute/hermetic-compute.h)
provides a simple API for creating a process, loading a hermetic compute engine
into it, launching the process, and waiting for it to exit.

An engine normally gets a vDSO so it can make system calls, but this is under
the control of the API.  In the future when more variant vDSOs are provided, a
vDSO with a drastically limited subset of system calls can be used.  Today the
norm is to expect the hermetic compute process to make no system calls other
than **zx_process_exit**, but it has a full vDSO that allows making any system
call.  Since it usually has no handles, most system calls can't be made to do
anything useful.  But various **..._create** calls can be made with no handle
and cause kernel resource consumption.  **vmo_create** and **vmo_write** calls
can lead to unbounded memory consumption, even though the process may have no
way to map a VMO into its own address space (since it doesn't have its own root
VMAR handle).

## Building hermetic compute engines

The **HermeticComputeEngine** class in
[<lib/hermetic-compute/hermetic-engine.h>](include/lib/hermetic-compute/hermetic-engine.h)
provides an API for defining a hermetic compute engine as a derived class.
This handles the startup protocol implemented by **HermeticComputeProcess** to
transparently forward arguments from the controlling process to the engine's
entry point.

**HermeticComputeProcess** does only basic ELF loading and does not perform any
kind of dynamic linking or relocation processing.  This means that engine code
must be **purely position independent**.  As well as having no shared library
dependencies, it cannot have any dynamic relocation requirements.  This means
e.g. no C initializers containing address constants.  It also means that even
dynamic-linking symbol references that get "resolved" at link time won't work;
i.e., no PLTs or GOT references whatsoever.  Normally any code that refers to
any variable with global (or class static) scope produces a GOT reference even
if the linker winds up resolving that locally.  (The `-fvisibility=hidden` flag
doesn't affect this behavior.)

Currently it is unreasonably difficult to build a hermetic compute engine, due
to these constraints.  We overload some of the build machinery intended for
[`userboot`](../../../docs/userboot.md) to build engines in the Zircon GN
build.  But it's extremely fiddly stuff.  In the future we will improve this in
multiple ways:
 1. Proper libc support for hermetic modules.
    This will obviate the current need to specially compile code like `memcpy`
    and `strlen` to be used in an engine.
 2. Full(er) relocation/dynamic-linking support.  Eventually we'll support
    out-of-process dynamic linking and it will be possible to load up an engine
    process with arbitrary code using shared libraries and so on.
Neither of these things will happen very soon.  So for the time being, it's
expected that the only engines written will be ones living in the Zircon source
tree.
