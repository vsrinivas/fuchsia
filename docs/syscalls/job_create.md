# mx_job_create

## NAME

job_create - create a new job

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_job_create(mx_handle_t job, uint32_t options, mx_handle_t* out);

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

**job_create**() returns MX_OK and a handle to the new job
(via *out*) on success.  In the event of failure, a negative error value
is returned.

## ERRORS

**MX_ERR_BAD_HANDLE**  *job* is not a valid handle.

**MX_ERR_WRONG_TYPE**  *job* is not a job handle.

**MX_ERR_INVALID_ARGS**  *options* is nonzero, or *out* is an invalid pointer.

**MX_ERR_ACCESS_DENIED**  *job* does not have the **MX_RIGHT_WRITE** right.

**MX_ERR_OUT_OF_RANGE**  The height of *job* is too large to create a child job.

**MX_ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**MX_ERR_BAD_STATE**  (Temporary) Failure due to the job object being in the
middle of a *mx_task_kill()* operation.

## SEE ALSO

[process_create](process_create.md),
[task_kill](task_kill.md),
[object_get_property](object_get_property.md).
