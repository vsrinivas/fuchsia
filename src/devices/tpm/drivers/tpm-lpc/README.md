# tpm-lpc
Implements a TPM LPC driver for engineering builds and testing. This
allows routing the swtpm from the host through QEMU so that is it can
be called inside Fuchsia. This is incredibly useful for testing
TPM functionality in end to end tests. This driver is generic and
should work with any x86 device with ACPI and a TPM on the Low Pin Count.

## Testing
Unit tests for tpm-lpc are available in the `tpm-lpc-tests`
package.

```
$ fx test tpm-lpc-tests
```
