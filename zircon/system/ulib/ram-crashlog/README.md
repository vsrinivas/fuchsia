## ram-crashlog

### Overview
ram_crashlog is a small library mean to be used by the kernel in order to
facilitate storage and retrieval of crashlogs which are kept in simple,
memory-mapable, RAM.  This RAM may be either dynamic ram, or some sort of
on-chip static RAM.  It does not really matter, provided that the RAM is memory
mapped and fast enough to be treated like any other RAM in the system.

### Restrictions
- The ram-crashlog library assumes single threaded semantics, and uses no
  locking primitives.  In other words, locking is the responsibility of the
  library's user.
- In a fuchsia kernel, during a stow operation, writebacks will be generated at
  appropriate points in time in order to be as transactional as possible, and to
  make certain that the system is ready for reboot at any point after a successful
  stow operation.  When used outside of Fuchsia, or in a Fuchsia user-mode
  application, no cache manipulation takes place.
