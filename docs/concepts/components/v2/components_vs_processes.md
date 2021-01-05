# Components vs. processes

This document explains how the concept of components differs from processes and
related concepts.

The Zircon kernel defines [processes][process] and other [task objects][tasks]
that are common in modern operating systems. The abstraction of
[component instances][topology.md#component-instances] sometimes correlates
with Zircon task abstractions, but not always.

## Examples

The relationship between components and Zircon tasks differs, often as defined
by [component runners][capabilities/runners.md] which implement strategies for launching
component instances.

-   [ELF Runner][elf_runner.md] launches components by creating a new
    [job][job] that contains a process that's created from a given executable
    file in ELF format.
-   Dart Runner launches a new Dart isolate in a Dart Virtual Machine. A Dart
    VM is implemented as a process that can host one or more Dart isolate.
    Dart isolates execute on [threads][thread], but don't necessarily have an
    assigned thread (this is a VM implementation detail).
-   Web runner can launch one or more web pages as components, and host them
    the same web engine container or in separate containers per its isolation
    policy. Web pages are typically isolated by being hosted in separate
    processes.

[job]: /docs/reference/kernel_objects/job.md
[process]: /docs/reference/kernel_objects/process.md
[thread]: /docs/reference/kernel_objects/thread.md
[tasks]: /docs/reference/kernel_objects/objects.md#tasks
