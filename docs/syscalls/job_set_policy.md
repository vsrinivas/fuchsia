# mx_job_set_policy

## NAME

job_set_policy - Set an job security and resource policies.

## SYNOPSIS

```
#include <magenta/syscalls.h>
#include <magenta/syscalls/policy.h>

mx_status_t mx_job_set_policy(mx_handle_t job_handle, uint32_t options,
                              uint32_t topic, void* policy, size_t count);

```

## DESCRIPTION

Sets a security and/or resource policy to an empty job. The job's effective policy
is the combination of the parent's effective policy and the policy specified in
*policy*. The effective policy is controlled by *options* values:

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

## RETURN VALUE

**mx_job_set_policy**() returns **NO_ERROR** on success.  In the event of failure,
a negative error value is returned.


## ERRORS

**ERR_INVALID_ARGS**  *policy* was not a valid pointer, or *count* was 0,
or *policy* was not **MX_JOB_POL_RELATIVE** or **MX_JOB_POL_ABSOLUTE**, or
*topic* was not **MX_JOB_POL_BASIC**.

**ERR_BAD_HANDLE**  *job_handle* is not valid handle.

**ERR_WRONG_HANDLE**  *job_handle* is not a job handle.

**ERR_ACCESS_DENIED**  *job_handle* does not have MX_RIGHT_SET_POLICY right.

**ERR_BAD_STATE**  the job has existing jobs or processes alive.

**ERR_NO_MEMORY**  (Temporary) Out of memory condition.

## SEE ALSO

[job_create](job_create.md).
[process_create](job_create.md).
