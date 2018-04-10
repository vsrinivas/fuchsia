# Static Analysis in Zircon

This document describes:

* How to perform static analysis with the Clang Static Analyzer in Zircon;
* How to enable ZirconHandleChecker;
* How to add/modify annotate attributes to syscalls/functions and use annotate attributes to suppress false positives.

## Steps to run Clang Static Analyzer

Assuming you already obtained a local copy of Fuchsia workspace according to the instructions written in [get_source.md](https://fuchsia.googlesource.com/docs/+/master/getting_source.md) and the source tree of fuchsia is located at `$LOCAL_DIR/fuchsia` and current working directory is `$LOCAL_DIR/fuchsia/zircon`. The Clang Static Analayzer can be run on Zircon by following commands:

```sh
./scripts/download-prebuilt
./scripts/analyze-zircon
```

The Clang Static Analyzer will be run on Zircon code base with default checkers. After the finish of the analysis, you can see an outout in stdout similar to the one below:

```
scan-build: Run 'scan-view $LOCAL_DIR/fuchsia/zircon/AnalysisResult/scan-build-2017-08-08-11-26-25-914570-SKSE39' to examine bug reports.
```

Just type the command start with `scan-view` in a terminal and it will open your web browser and show the analysis reports.

## Steps to enable ZirconHandleChecker

At the time this document is written, all Zircon related checkers are still under review by upstream LLVM community:

 * MutexInInterruptContext [D27854](https://reviews.llvm.org/D27854)
 * SpinLockChecker [D26340](https://reviews.llvm.org/D26340)
 * MutexChecker [D26342](https://reviews.llvm.org/D26342)
 * ZirconHandleChecker [D35968](https://reviews.llvm.org/D35968) [D36022](https://reviews.llvm.org/D36022) [D36023](https://reviews.llvm.org/D36023) [D36024](https://reviews.llvm.org/D36024) [D36251](https://reviews.llvm.org/D36251) [D36475](https://reviews.llvm.org/D36475))

They are enabled by default when you executed the 'analyze-zircon' script. We will update the 'analyze-zircon' script to enable them by default once they get landed.

In the mean time, if you would like to try ZirconHandleChecker now, you can download the source code of LLVM with Clang and apply the patch from the diffs above and follow the instructions in [toolchain.md](https://fuchsia.googlesource.com/docs/+/master/toolchain.md) to build your own toolchain. Assuming you have built your own toolchain and it is located at `$LOCAL_TOOLCHAIN_PREFIX` and `$LOCAL_TOOLCHAIN_PREFIX/bin/clang` is the path to the `clang` command. The Clang Static Analyzer can be run with ZirconHandleChecker and other default checkers enabled by following command:

```
./scripts/analyze-zircon -p $LOCAL_TOOLCHAIN_PREFIX -m all
```

If you want to enable ZirconHandleChecker and disable other default checkers, please run following command:

```
./scripts/analyze-zircon -p $LOCAL_TOOLCHAIN_PREFIX -m zircon
```

The 'analyze-zircon' scripts have additional options such as changing the output directories and changing build targets, please refer the to help information printed by `./scripts/analyze-zircon -h`.

## Steps to add/modify annotate attributes to syscalls/functions

In Zircon code base, raw annotations like `__attribute__((annotate("string")))` should never be used in Zircon code base, all zircon related annotations should be wrapped by macros. In this section, we will discuss how to add or modify annotations in Zircon code base.

### Annotations in syscall declaration

As header files of Zircon syscalls are generated from syscalls.abigen, in order to add/modify annotations of syscalls, the syscalls.abigen should be modified directly.
Let’s use `zx_channel_create syscall` as example. This syscall will allocate two handles when it is successfully executed. Without annotations, its declaration in abigen will be like:

```c
syscall channel_create
    (options: uint32_t)
    returns (zx_status_t, out0: zx_handle_t, out1: zx_handle_t);
```

As argument `out0` and `out1` will be allocated handles, we should add `handle_acquire` annotation to these arguments:

```c
syscall channel_create
    (options: uint32_t)
    returns (zx_status_t, out0: zx_handle_t handle_acquire,
             out1: zx_handle_t handle_acquire);
```

This syscall declaration will be processed by abigen and converted to:

```c
extern zx_status_t zx_channel_create(
uint32_t options,
    ZX_SYSCALL_PARAM_ATTR(handle_acquire) zx_handle_t* out0,
    ZX_SYSCALL_PARAM_ATTR(handle_acquire) zx_handle_t* out1));
```

The declaration of macro can be found in system/public/zircon/syscalls.h, which is:

```c
#if defined(__clang__)
#define ZX_SYSCALL_PARAM_ATTR(x)   __attribute__((annotate("zx_" #x)))
#else
#define ZX_SYSCALL_PARAM_ATTR(x)   // no-op
#endif
```

According to the definition of `ZX_SYSCALL_PARAM_ATTR`, the `zx_channel_create` will be parsed into:

```c
extern zx_status_t zx_channel_create(uint32_t options,
__attribute__((annotate("zx_handle_acquire"))) zx_handle_t* out0,
__attribute__((annotate("zx_handle_acquire"))) zx_handle_t* out1) __attribute__((__leaf__));;
```

The reason that we use macros to wrap these annotations is that annotate attribute is not supported by compilers other than Clang, e.g. GCC. Furthermore, it would be convenient if we decide to use annotation solutions other than the annotate attributes in the future. Otherwise we need to change each annotation one by one.

### Annotations in other functions

For functions other than syscalls, if `system/public/zircon/syscalls.h` is in current include path, you can use `ZX_SYSCALL_PARAM_ATTR` macro to wrap your annotations. If not, you should use macros similar to this one. The reason that functions other than syscalls may require annotations is that some functions contain known false positives and we can use annotation to suppress the warnings of these false positives. For example, in ZirconHandleChecker’s test file we have:

```c
#if defined(__clang__)
#define ZX_ANALYZER_SUPPRESS   __attribute__((annotate("zx_suppress_warning)))
#else
#define ZX_ANALYZER_SUPPRESS   // no-op
#endif
void checkSuppressWarning() ZX_ANALYZER_SUPPRESS {
  zx_handle_t sa, sb;
  if (zx_channel_create(0, &sa, &sb) < 0) {
    return;
  }
  zx_handle_close(sa); // Should not report any bugs here
}
```

The analyzer will suppress the warnings on the bug it discovered in `checkSuppressWarning` function. If you don’t want to define your own macro for this purpose, and the `syscalls.h` is in the include path, you can use `_SYSCALL_PARAM_ATTR(suppress_warning)` instead, it will suppress the warnings of all bugs discovered in the functions with this annotation.

Similar to `zx_suppress_warning` annotation, we have `zx_create_sink` annotation which currently used to suppress warnings on assertion failures. This annotation is unlikely to be used for other purpose, however, if you would like to know how it works, please refer to the discussions in CL[46428](https://fuchsia-review.googlesource.com/c/46428).

To manually annotate non-syscall functions, the "ZX_SYSCALL_PARAM_ATTR" macro can be applied to function arguments, emulating the effect of the abigen attributes. For example, here, we annotate a regular function which might be used to call the "zx_create_channel" function without passing the "options" argument:

```c
zx_status_t create_channel(
  ZX_SYSCALL_PARAM_ATTR(handle_acquire) zx_handle_t* out0,
  ZX_SYSCALL_PARAM_ATTR(handle_acquire) zx_handle_t* out1);
```
Another example, we have another function `takeover_handle` that will take care the lifecycle of a handle if it is successfully executed and do nothing if it failed, we can declare this function in header file like this:

```c
zx_status_t takeover_handle(
  ZX_SYSCALL_PARAM_ATTR(handle_escape) zx_handle_t in)
  ZX_SYSCALL_PARAM_ATTR(may_fail);
```

The `zx_may_fail` annotation here will cause state bifurcation when ZirconHandleChecker is evaluating calls to this function. So both succeeded and failed states will be covered.

If the `ZX_SYSCALL_PARAM_ATTR` is not available in the file that declares the function, you can define your own macros, as long as it will not expanded into annotate attribute if it is not compiled by Clang.
