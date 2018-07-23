# zx_smc_call

## NAME

smc_call - Make Secure Monitor Call (SMC) from user space

## SYNOPSIS

```
#include <zircon/syscalls.h>
#include <zircon/syscalls/smc.h>

typedef struct zx_smc_parameters {
    uint32_t func_id;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
    uint64_t arg6;
    uint16_t client_id;
    uint16_t secure_os_id;
} zx_smc_parameters_t;

typedef struct zx_smc_result {
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
} zx_smc_result_t;

zx_status_t zx_smc_call(zx_handle_t handle,
                        const zx_smc_parameters_t* parameters,
                        zx_smc_result_t* out_smc_result);
```

## DESCRIPTION

**smc_call**() makes a Secure Monitor Call (SMC) from user space. It supports the ARM SMC Calling
Convention using the *zx_smc_parameters_t* input parameter and *zx_smc_result_t* output parameter.
The input *handle* must be a resource object with sufficient privileges in order to be executed.

The majority of the parameters are opaque from **smc_call** perspective because they are
dependent upon the *func_id*. The *func_id* informs the Secure Monitor the service and function
to be invoked. The *client_id* is an optional field intended for secure software to track and
index the calling client OS. The *secure_os_id* is an optional field intended for use when there
are multiple secure operating systems at S-EL1, so that the caller may specify the intended
secure OS.

More information is available in the [ARM SMC Calling Convention documentation](
http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0028b/index.html).


## RIGHTS

TODO(ZX-2399)

## RETURN VALUE

**smc_call**() returns ZX_OK if *handle* has sufficient privilege. The
return value of the smc call is returned via **out_smc_result** on success. In the event of
failure, a negative error value is returned.

## ERRORS

**ZX_ERR_BAD_HANDLE**  *handle* is not a valid handle.

**ZX_ERR_WRONG_TYPE**  *handle* is not a resource handle.

**ZX_ERR_ACCESS_DENIED**  *handle* does not have sufficient privileges.

**ZX_ERR_NOT_SUPPORTED**  smc_call is not supported on this system.

**ZX_ERR_INVALID_ARGS**  *parameters* or *out_smc_result* a null pointer
