# Zircon Kernel Commandline Options

TODO([fxbug.dev/53594](https://fxbug.dev/53594)): move kernel_cmdline.md verbiage here

## Options common to all machines

### aslr.disable=\<bool>
**Default:** `false`

If this option is set, the system will not use Address Space Layout
Randomization.

### aslr.entropy_bits=\<uint8_t>
**Default:** `0x1e`

For address spaces that use ASLR this controls the number of bits of entropy in
the randomization. Higher entropy results in a sparser address space and uses
more memory for page tables. Valid values range from 0-36.

### kernel.entropy-mixin=\<hexadecimal>

Provides entropy to be mixed into the kernel's CPRNG.  The value must be a
string of lowercase hexadecimal digits.

The original value will be scrubbed from memory as soon as possible and will be
redacted from all diagnostic output.

### kernel.jitterentropy.bs=\<uint32_t>
**Default:** `0x40`

Sets the "memory block size" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), the memory
will be accessed in blocks of this size.

### kernel.jitterentropy.bc=\<uint32_t>
**Default:** `0x200`

Sets the "memory block count" parameter for jitterentropy. When jitterentropy
is performing memory operations (to increase variation in CPU timing), this
controls how many blocks (of size `kernel.jitterentropy.bs`) are accessed.

### kernel.jitterentropy.ml=\<uint32_t>
**Default:** `0x20`

Sets the "memory loops" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), this
controls how many times the memory access routine is repeated. This parameter
is only used when `kernel.jitterentropy.raw` is true. If the value of this
parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.

### kernel.jitterentropy.ll=\<uint32_t>
**Default:** `0x1`

Sets the "LFSR loops" parameter for jitterentropy (the default is 1). When
jitterentropy is performing CPU-intensive LFSR operations (to increase variation
in CPU timing), this controls how many times the LFSR routine is repeated.  This
parameter is only used when `kernel.jitterentropy.raw` is true. If the value of
this parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.

### kernel.jitterentropy.raw=\<bool>
**Default:** `true`

When true (the default), the jitterentropy entropy collector will return raw,
unprocessed samples. When false, the raw samples will be processed by
jitterentropy, producing output data that looks closer to uniformly random. Note
that even when set to false, the CPRNG will re-process the samples, so the
processing inside of jitterentropy is somewhat redundant.

### kernel.serial=[none | legacy | qemu | \<type>,\<base>,\<irq>]
**Default:** `none`

TODO(53594)


## Options available only on arm64 machines

### kernel.arm64.disable_spec_mitigations=\<bool>
**Default:** `false`

If set, disables all speculative execution information leak mitigations.

If unset, the per-mitigation defaults will be used.

### kernel.arm64.event-stream.enable=\<bool>
**Default:** `false`

When enabled, each ARM cpu will enable an event stream generator, which per-cpu
sets the hidden event flag at a particular rate. This has the effect of kicking
cpus out of any WFE states they may be sitting in.

### kernel.arm64.event-stream.freq-hz=\<uint32_t>
**Default:** `0x2710`

If the event stream is enabled, specifies the frequency at which it will attempt
to run. The resolution is limited, so the driver will only be able to pick the
nearest power of 2 from the cpu timer counter.


## Options available only on x86 machines

### kernel.x86.disable_spec_mitigations=\<bool>
**Default:** `false`

If set, disables all speculative execution information leak mitigations.

If unset, the per-mitigation defaults will be used.

### kernel.x86.md_clear_on_user_return=\<bool>
**Default:** `true`

MDS (Microarchitectural Data Sampling) is a family of speculative execution
information leak bugs that allow the contents of recent loads or stores to be
inferred by hostile code, regardless of privilege level (CVE-2019-11091,
CVE-2018-12126, CVE-2018-12130, CVE-2018-12127). For example, this could allow
user code to read recent kernel loads/stores.

To avoid this bug, it is required that all microarchitectural structures
that could leak data be flushed on trust level transitions. Also, it is
important that trust levels do not concurrently execute on a single physical
processor core.

This option controls whether microarchitectual structures are flushed on
the kernel to user exit path, if possible. It may have a negative performance
impact.

*   If set to true (the default), structures are flushed if the processor is
    vulnerable.
*   If set to false, no flush is executed on structures.

### kernel.x86.pti.enable=\<uint32_t>
**Default:** `0x2`

Page table isolation configures user page tables to not have kernel text or
data mapped. This may impact performance negatively. This is a mitigation
for Meltdown (AKA CVE-2017-5754).

* If set to 1, this force-enables page table isolation.
* If set to 0, this force-disables page table isolation. This may be insecure.
* If set to 2 or unset (the default), this enables page table isolation on
CPUs vulnerable to Meltdown.

TODO(joshuaseaton): make this an enum instead of using magic integers.

### kernel.x86.spec_store_bypass_disable=\<bool>
**Default:** `false`

Spec-store-bypass (Spectre V4) is a speculative execution information leak
vulnerability that affects many Intel and AMD x86 CPUs. It targets memory
disambiguation hardware to infer the contents of recent stores. The attack
only affects same-privilege-level, intra-process data.

This command line option controls whether a mitigation is enabled. The
mitigation has negative performance impacts.

* If true, the mitigation is enabled on CPUs that need it.
* If false (the default), the mitigation is not enabled.

TODO: put something here
