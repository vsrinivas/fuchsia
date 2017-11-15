# zx_cache_flush

## NAME

zx_cache_flush - Flush CPU data and/or instruction caches

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_cache_flush(const void* addr, size_t len, uint32_t flags);
```

## DESCRIPTION

**zx_cache_flush**() flushes CPU caches covering memory in the given
virtual address range.  If that range of memory is not readable, then
the thread may fault as it would for a data read.

*flags* is a bitwise OR of:

 * **ZX_CACHE_FLUSH_DATA**

   Clean (write back) data caches, so previous writes on this CPU are
   visible in main memory.

 * **ZX_CACHE_FLUSH_INVALIDATE**
   (valid only when combined with **ZX_CACHE_FLUSH_DATA**)

   Clean (write back) data caches and then invalidate data caches, so
   previous writes on this CPU are visible in main memory and future
   reads on this CPU see external changes to main memory.

 * **ZX_CACHE_FLUSH_INSN**

   Synchronize instruction caches with data caches, so previous writes
   on this CPU are visible to instruction fetches.  If this is combined
   with **ZX_CACHE_FLUSH_DATA**, then previous writes will be visible to
   main memory as well as to instruction fetches.

At least one of **ZX_CACHE_FLUSH_DATA** and **ZX_CACHE_FLUSH_INSN**
must be included in *flags*.

## RETURN VALUE

**zx_cache_flush**() returns **ZX_OK** on success, or an error code on failure.

## ERRORS

**ZX_ERR_INVALID_ARGS** *flags* is invalid.
