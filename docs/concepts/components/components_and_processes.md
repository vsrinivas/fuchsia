# The difference between components and processes

This document explains how components relate to jobs, processes, and threads.

There is no inherent relationship between [component instances](introduction.md)
and a Zircon task (job, process, or thread). It's best to avoid a mental model
with a fixed relationship between components and Zircon tasks.

## No direct relationship

There's no inherent relationship between Zircon tasks and components.

Note: Compare with section [Dynamic relationships](#dynamic-relationships).

To illustrate that there is no inherent relationship, consider that a component
may:

- Have zero or more jobs.
- Have zero or more processes.
- Have zero or more threads.
- Share a job with other components.
- Share a process with other components.
- Share a thread with other components.

Different components are expressed, or implemented, differently (even in ways we
haven't yet explored).

## Dynamic relationships {#dynamic-relationships}

The way components and Zircon tasks relate is dynamic. On initial inspection it
may appear that there is a hierarchy, but there is no hierarchy between
components and processes.

## Examples

Here are some examples of specific component types to illustrate the nature of
the component abstraction:

- Dart Runner
    - The Dart runner, which itself is a component, is a single process that
      runs separate Dart components in separate threads.
- ELF binaries
    - The [ELF runner](elf_runner.md), which itself is a component, starts a
      process to kick off the component and then lets that process spawn
      additional processes as part of the same component.
- Web page components (using Web Runner)
    - The Web Runner uses multiple processes in a single component.
