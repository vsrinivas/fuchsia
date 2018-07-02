# zx_job_set_policy

## NAME

job_set_policy - Set job security and resource policies.

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/policy.h>

zx_status_t zx_job_set_policy(zx_handle_t job_handle, uint32_t options,
                              uint32_t topic, const void* policy, uint32_t count);

```

## DESCRIPTION

Sets one or more security and/or resource policies to an empty job. The job's
effective policies is the combination of the parent's effective policies and
the policies specified in *policy*. The effect in the case of conflict between
the existing policies and the new policies is controlled by *options* values:

+ **ZX_JOB_POL_RELATIVE** : policy is applied for the conditions not specifically
  overridden by the parent policy.
+ **ZX_JOB_POL_ABSOLUTE** : policy is applied for all conditions in *policy* or
  the syscall fails.

After this call succeeds any new child process or child job will have the new
effective policy applied to it.

*topic* indicates the *policy* format. Supported value is **ZX_JOB_POL_BASIC**
which indicates that *policy* is an array of *count* entries of:

```
typedef struct zx_policy_basic {
    uint32_t condition;
    uint32_t policy;
} zx_policy_basic_t;

```

Where *condition* is one of
+ **ZX_POL_BAD_HANDLE** a process under this job is attempting to
  issue a syscall with an invalid handle.  In this case,
  **ZX_POL_ACTION_ALLOW** and **ZX_POL_ACTION_DENY** are equivalent:
  if the syscall returns, it will always return the error
  **ZX_ERR_BAD_HANDLE**.
+ **ZX_POL_WRONG_OBJECT** a process under this job is attempting to
  issue a syscall with a handle that does not support such operation.
+ **ZX_POL_VMAR_WX** a process under this job is attempting to map an
  address region with write-execute access.
+ **ZX_POL_NEW_VMO** a process under this job is attempting to create
  a new vm object.
+ **ZX_POL_NEW_CHANNEL** a process under this job is attempting to create
  a new channel.
+ **ZX_POL_NEW_EVENT** a process under this job is attempting to create
  a new event.
+ **ZX_POL_NEW_EVENTPAIR** a process under this job is attempting to create
  a new event pair.
+ **ZX_POL_NEW_PORT** a process under this job is attempting to create
  a new port.
+ **ZX_POL_NEW_SOCKET** a process under this job is attempting to create
  a new socket.
+ **ZX_POL_NEW_FIFO** a process under this job is attempting to create
  a new fifo.
+ **ZX_POL_NEW_TIMER** a process under this job is attempting to create
  a new timer.
+ **ZX_POL_NEW_PROCESS** a process under this job is attempting to create
  a new process.
+ **ZX_POL_NEW_ANY** is a special *condition* that stands for all of
  the above **ZX_NEW** condtions such as **ZX_POL_NEW_VMO**,
  **ZX_POL_NEW_CHANNEL**, **ZX_POL_NEW_EVENT**, **ZX_POL_NEW_EVENTPAIR**,
  **ZX_POL_NEW_PORT**, **ZX_POL_NEW_SOCKET**, **ZX_POL_NEW_FIFO**,
  and any future ZX_NEW policy. This will include any new
  kernel objects which do not require a parent object for creation.

Where *policy* is either
+ **ZX_POL_ACTION_ALLOW**  allow *condition*.
+ **ZX_POL_ACTION_DENY**  prevent *condition*.

Optionally it can be augmented via OR with
+ **ZX_POL_ACTION_EXCEPTION** generate an exception via the debug port. An
  exception generated this way acts as a breakpoint. The thread may be
  resumed after the exception.
+ **ZX_POL_ACTION_KILL** terminate the process. It also
implies **ZX_POL_ACTION_DENY**.

## RETURN VALUE

**zx_job_set_policy**() returns **ZX_OK** on success.  In the event of failure,
a negative error value is returned.

## NOTES

The **ZX_POL_BAD_HANDLE** policy does not apply when calling ``zx_object_get_info()``
with the topic ZX_INFO_HANDLE_VALID.  All other topics and all other syscalls that
take handles are subject to the policy.

## ERRORS

**ZX_ERR_INVALID_ARGS**  *policy* was not a valid pointer, or *count* was 0,
or *policy* was not **ZX_JOB_POL_RELATIVE** or **ZX_JOB_POL_ABSOLUTE**, or
*topic* was not **ZX_JOB_POL_BASIC**.

**ZX_ERR_BAD_HANDLE**  *job_handle* is not valid handle.

**ZX_ERR_WRONG_TYPE**  *job_handle* is not a job handle.

**ZX_ERR_ACCESS_DENIED**  *job_handle* does not have ZX_POL_RIGHT_SET right.

**ZX_ERR_BAD_STATE**  the job has existing jobs or processes alive.

**ZX_ERR_OUT_OF_RANGE** *count* is bigger than ZX_POL_MAX or *condition* is
bigger than ZX_POL_MAX.

**ZX_ERR_ALREADY_EXISTS** existing policy conflicts with the new policy.

**ZX_ERR_NOT_SUPPORTED** an entry in *policy* has an invalid value.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.
There is no good way for userspace to handle this (unlikely) error.
In a future build this error will no longer occur.

## SEE ALSO

[job_create](job_create.md).
[process_create](job_create.md).
[object_get_info](object_get_info.md).
