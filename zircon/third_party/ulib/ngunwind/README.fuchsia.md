This unwinder is based on the nongnu libunwind.

http://download.savannah.gnu.org/releases/libunwind/libunwind-1.2-rc1.tar.gz

To make the code more tractable:
- all the macro magic has been deleted
- unused ports have been deleted
- headers have been moved to follow zircon's scheme

At the present UNW_LOCAL_ONLY support is gone, Fuchsia uses the llvm
unwinder for that. Ultimately, it is intended to switch Fuchsia to use
the llvm unwinder for remote unwinding as well, when the support is
available.
