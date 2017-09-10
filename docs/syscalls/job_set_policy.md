# mx_job_set_policy

## NAME

job_set_policy - Set job security and resource policies.

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/policy.h>

mx_status_t mx_job_set_policy(mx_handle_t job_handle, uint32_t options,
                              uint32_t topic, void* policy, size_t count);

```

## DESCRIPTION

Sets one or more security and/or resource policies to an empty job. The job's
effective policies is the combination of the parent's effective policies and
the policies specified in *policy*. The effect in the case of conflict between
the existing policies and the new policies is controlled by *options* values:

+ **MX_JOB_POL_RELATIVE** : policy is applied for the conditions not specifically
  overridden by the parent policy.
+ **MX_JOB_POL_ABSOLUTE** : policy is applied for all conditions in *policy* or
  the syscall fails.

After this call succeeds any new child process or child job will have the new
effective policy applied to it.

*topic* indicates the *policy* format. Supported value is **MX_JOB_POL_BASIC**
which indicates that *policy* is an array of *count* entries of:

```
typedef struct mx_policy_basic {
    uint32_t condition;
    uint32_t policy;
} mx_policy_basic_t;

```

Where *condition* is one of
+ **MX_POL_BAD_HANDLE** a process under this job is attempting to
  issue a syscall with an invalid handle.  In this case,
  **MX_POL_ACTION_ALLOW** and **MX_POL_ACTION_DENY** are equivalent:
  if the syscall returns, it will always return the error
  **MX_ERR_BAD_HANDLE**.
+ **MX_POL_WRONG_OBJECT** a process under this job is attempting to
  issue a syscall with a handle that does not support such operation.
+ **MX_POL_VMAR_WX** a process under this job is attempting to map an
  address region with write-execute access.
+ **MX_POL_NEW_VMO** a process under this job is attempting to create
  a new vm object.
+ **MX_POL_NEW_CHANNEL** a process under this job is attempting to create
  a new channel.
+ **MX_POL_NEW_EVENT** a process under this job is attempting to create
  a new event.
+ **MX_POL_NEW_EVPAIR** a process under this job is attempting to create
  a new event pair.
+ **MX_POL_NEW_PORT** a process under this job is attempting to create
  a new port.
+ **MX_POL_NEW_SOCKET** a process under this job is attempting to create
  a new socket.
+ **MX_POL_NEW_FIFO** a process under this job is attempting to create
  a new fifo.
+ **MX_POL_NEW_TIMER** a process under this job is attempting to create
  a new timer.
+ **MX_POL_NEW_ANY** is a special *condition* that stands for all of
  the above **MX_NEW** condtions such as **MX_POL_NEW_VMO**,
  **MX_POL_NEW_CHANNEL**, **MX_POL_NEW_EVENT**, **MX_POL_NEW_EVPAIR**,
  **MX_POL_NEW_PORT**, **MX_POL_NEW_SOCKET**, **MX_POL_NEW_FIFO**,
  **MX_POL_NEW_GUEST**, and any future MX_NEW policy. This will include any new
  kernel objects which do not require a parent object for creation.

Where *policy* is either
+ **MX_POL_ACTION_ALLOW**  allow *condition*.
+ **MX_POL_ACTION_DENY**  prevent *condition*.

Optionally it can be augmented via OR with
+ **MX_POL_ACTION_EXCEPTION** generate an exception via the debug port. An
  exception generated this way acts as a breakpoint. The thread may be
  resumed after the exception.
+ **MX_POL_ACTION_KILL** terminate the process. It also
implies **MX_POL_ACTION_DENY**.

## RETURN VALUE

**mx_job_set_policy**() returns **MX_OK** on success.  In the event of failure,
a negative error value is returned.


## ERRORS

**MX_ERR_INVALID_ARGS**  *policy* was not a valid pointer, or *count* was 0,
or *policy* was not **MX_JOB_POL_RELATIVE** or **MX_JOB_POL_ABSOLUTE**, or
*topic* was not **MX_JOB_POL_BASIC**.

**MX_ERR_BAD_HANDLE**  *job_handle* is not valid handle.

**MX_ERR_WRONG_TYPE**  *job_handle* is not a job handle.

**MX_ERR_ACCESS_DENIED**  *job_handle* does not have MX_POL_RIGHT_SET right.

**MX_ERR_BAD_STATE**  the job has existing jobs or processes alive.

**MX_ERR_OUT_OF_RANGE** *count* is bigger than MX_MAX_POLICY.

**MX_ERR_ALREADY_EXISTS** existing policy conflicts with the new policy.

**MX_ERR_NOT_SUPPORTED** an entry in *policy* has an invalid value.

**MX_ERR_NO_MEMORY**  (Temporary) Out of memory condition.

## SEE ALSO

[job_create](job_create.md).
[process_create](job_create.md).
