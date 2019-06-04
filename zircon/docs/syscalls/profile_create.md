# zx_profile_create

## NAME

<!-- Updated by update-docs-from-abigen, do not edit. -->

Create a scheduler profile.

## SYNOPSIS

<!-- Updated by update-docs-from-abigen, do not edit. -->

```c
#include <zircon/syscalls.h>

zx_status_t zx_profile_create(zx_handle_t root_job,
                              const zx_profile_info_t* profile,
                              zx_handle_t* out);
```

## DESCRIPTION

`zx_profile_create()` creates a new [profile](../objects/profile.md) object.

The parameter *profile* specifies the settings in the profile, which in turn
will be applied to threads when `zx_object_set_profile()` is called. While the
type `zx_profile_info_t` has support for additional settings in the future,
currently only `priority` is implemented in the kernel.

A profile specifying a custom profile is specified as follows:

```c
zx_profile_info_t profile_info = {
  .type = ZX_PROFILE_INFO_SCHEDULER,
  .scheduler = {
    .priority = n,  // Valid priorities are 0 -- 31, inclusive.
  },
};
```

Upon success a handle for the new profile is returned.

## RIGHTS

<!-- Updated by update-docs-from-abigen, do not edit. -->

*root_job* must be of type **ZX_OBJ_TYPE_JOB** and have **ZX_RIGHT_MANAGE_PROCESS**.

## RETURN VALUE

Returns **ZX_OK** and a handle to the new profile (via *out*) on success. In the
event of failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *root_job* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *root_job* is not a job handle.

**ZX_ERR_ACCESS_DENIED**  *root_job* does not have **ZX_RIGHT_MANAGE_PROCESS**
right, or *root_job* is not a handle to the root job.

**ZX_ERR_INVALID_ARGS**  *profile* or *out* was an invalid pointer, or
*info.scheduler.priority* was an invalid priority.

**ZX_ERR_NOT_SUPPORTED**  *info.type* was set to a value other than
**ZX_PROFILE_INFO_SCHEDULER**.

**ZX_ERR_NO_MEMORY**  Failure due to lack of memory.

## SEE ALSO

 - [`zx_object_set_profile()`]

<!-- References updated by update-docs-from-abigen, do not edit. -->

[`zx_object_set_profile()`]: object_set_profile.md
