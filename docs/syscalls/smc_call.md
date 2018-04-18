# zx_smc_call

## NAME

smc_call - Make Secure Monitor Call (SMC) from user space

## SYNOPSIS

```
#include <zircon/syscalls.h>

zx_status_t zx_smc_call(zx_handle_t root_rsrc,
                            uint64_t arg0, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3, uint64_t* out_smc_status);
```

## DESCRIPTION

**smc_call**() makes an SMC call from user space.

The four input arguments (*arg0*, *arg1*, *arg2* and *arg3*) will be passed directly to the
smc call.

## RETURN VALUE

**smc_call**() returns ZX_OK if root_rsrc has sufficient privilege. The
return value of the smc call is returned via **out_smc_status** on success. In the event of
failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *root_rsrc* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *root_rsrc* is not a resource handle.

**ZX_ERR_ACCESS_DENIED**  *root_rsrc* handle does not have sufficient privileges.

**ZX_ERR_NOT_SUPPORTED**  smc_call is not supported on this system.

**ZX_ERR_INVALID_ARGS**  *out_smc_status* a null pointer
