# Zircon Kernel Commandline Options

TODO([fxbug.dev/53594](https://fxbug.dev/53594)): move kernel_cmdline.md verbiage here

## Options common to all machines

### kernel.entropy-mixin=\<hexadecimal>

Provides entropy to be mixed into the kernel's CPRNG.  The value must be a
string of lowercase hexadecimal digits.

The original value will be scrubbed from memory as soon as possible and will be
redacted from all diagnostic output.


## Options available only on x86 machines

### kernel.x86.disable_spec_mitigations=\<bool>
**Default:** `false`

TODO(53593)

### kernel.x86.md_clear_on_user_return=\<bool>
**Default:** `true`

TODO(53593)

### kernel.x86.pti.enable=\<uint32_t>
**Default:** `0x2`

TODO(53593)

### kernel.x86.spec_store_bypass_disable=\<bool>
**Default:** `false`

TODO(53593)

TODO: put something here
