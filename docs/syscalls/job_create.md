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

It is illegal to create a job under a parent whose **MX_PROP_JOB_MAX_HEIGHT**
property is zero. (It is, however, legal to create a process under a parent with
*MAX_HEIGHT* zero.) See [object_get_property()](object_get_property.md).

Job handles may be waited on (TODO(cpu): expand this)

## RETURN VALUE

**job_create**() returns NO_ERROR and a handle to the new job
(via *out*) on success.  In the event of failure, a negative error value
is returned.

## ERRORS

**ERR_BAD_HANDLE**  *job* is not a valid handle.

**ERR_WRONG_TYPE**  *job* is not a job handle.

**ERR_INVALID_ARGS**  *options* is nonzero, or *out* is an invalid pointer.

**ERR_ACCESS_DENIED**  *job* does not have the **MX_RIGHT_WRITE** right.

**ERR_OUT_OF_RANGE**  *job* has a **MX_PROP_JOB_MAX_HEIGHT** property value
of zero.

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

**ERR_BAD_STATE**  (Temporary) Failure due to the job object being in the
middle of a *mx_task_kill()* operation.

## SEE ALSO

[process_create](process_create.md),
[task_kill](task_kill.md),
[object_get_property](object_get_property.md).
