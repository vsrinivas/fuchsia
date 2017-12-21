# zx_job_set_relative_importance

## NAME

job_set_relative_importance - update a global ordering of jobs

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_job_set_relative_importance(
    zx_handle_t resource, zx_handle_t job, zx_handle_t less_important_job)

```

## DESCRIPTION

*** note
**NOTE**: This is a low-level syscall that should not be used by typical user
processes.
***

**job_set_relative_importance**() indicates to the kernel how important a [job
object](../objects/job.md) is relative to another job.

Upon success, updates a partial ordering between jobs so that
*less_important_job* will be killed before *job* in low-resource situations. If
*less_important_job* is **ZX_HANDLE_INVALID**, then *job* becomes the
least-important job in the system.

If the new order pair would create a cycle, an existing order pair will be
invalidated to break the cycle.

*resource* must be a handle to the root resource, and is used as proof that the
caller has permission to perform this operation.

Since *resource* is used to control access to this operation, the handle rights
(for any argument) do not matter.

## RETURN VALUE

**job_set_relative_importance**() returns ZX_OK on success. In the event of
failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE** *resource* or *job* is not a valid handle.

**ZX_ERR_WRONG_TYPE** *resource* is not a resource handle, *job* is not a job
handle, or *less_important_job* is not a job handle (if it is a value other than
**ZX_HANDLE_INVALID**).

## SEE ALSO

[job_create](job_create.md)
