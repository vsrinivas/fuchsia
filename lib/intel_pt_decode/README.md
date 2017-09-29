# Intel Processor Trace Decoder

This library is used to read Intel Processor Trace files.

## Notes on the implementation

Several pieces are needed to properly interpret IPT traces.
The trace format is highly condensed, relying on offline access to the
code that was running. IPT can be used in a way to only trace a particular
program, or even particular threads. But when doing full system tracing
the decoder needs access to all the binaries. The traces record cr3 values
to allow the decoder to know what program the trace is for. 
Part of the complexity is from needing to take random cr3 values and mapping
them to the ELF. The Fuchsia build system creates "ids.txt" files that map
build ids to the ELF. On top of that the dynamic linker has a tracing mode
that can print build ids for each loaded ELF (binary or shared library),
and where in the address space they were loaded.
But the dynamic linker only knows a program by its pid. The final piece
is to have the kernel emit cr3->pid mappings, which is done via ktrace.
With this information we can then take any cr3 value and any pc value within
that address space, and find the associated ELF.
