# zx_job_create

## NAME

job_create - create a new job

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_job_create(zx_handle_t job, uint32_t options, zx_handle_t* out);

```

## DESCRIPTION

**job_create**() creates a new child [job object](../objects/job.md) given a
parent job.

Upon success a handle for the new job is returned.

The kernel keeps track of and restricts the "height" of a job, which is its
distance from the root job. It is illegal to create a job under a parent whose
height exceeds an internal "max height" value. (It is, however, legal to create
a process under such a job.)

Job handles may be waited on (TODO(cpu): expand this)

## RETURN VALUE

**job_create**() returns ZX_OK and a handle to the new job
(via *out*) on success.  In the event of failure, a negative error value
is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *job* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *job* is not a job handle.

**ZX_ERR_INVALID_ARGS**  *options* is nonzero, or *out* is an invalid pointer.

**ZX_ERR_ACCESS_DENIED**  *job* does not have the **ZX_RIGHT_WRITE** right.

**ZX_ERR_OUT_OF_RANGE**  The height of *job* is too large to create a child job.

**ZX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ZX_ERR_BAD_STATE**  The parent job object is in the dead state.

## SEE ALSO

[process_create](process_create.md),
[task_kill](task_kill.md),
[object_get_property](object_get_property.md).
