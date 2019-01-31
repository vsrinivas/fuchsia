Trace Benchmark
===============

Measures how long it takes to perform certain time-sensitive operations related
to tracing, such as the overhead of instrumentation when tracing is disabled.

Typical results (on real hardware -- not in QEMU) should show an instrumentation
overhead of a few nanoseconds when tracing is disabled and a few tens to
hundreds of nanoseconds when tracing is enabled depending on the complexity
of the record being written.
