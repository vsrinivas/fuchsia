# mx_job_create

## NAME

job_create - create a new job

## SYNOPSIS

```
#include <magenta/syscalls.h>

mx_status_t mx_job_create(mx_handle_t job, uint32_t flags, mx_handle_t* out);

```

## DESCRIPTION

**job_create**() creates a new child [job object](../objects/job.md) given a
parent job.

Upon success a handle for the new job is returned.

Job handles may be waited on (TODO: expand this)

## RETURN VALUE

**job_create**() returns NO_ERROR and a handle to the new job
(via *out*) on success.  In the event of failure, a negative error value
is returned.

## ERRORS

**ERR_NO_MEMORY**  (Temporary) Failure due to lack of memory.

## SEE ALSO

[process_create](process_create.md).
