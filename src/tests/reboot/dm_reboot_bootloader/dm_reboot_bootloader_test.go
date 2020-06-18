package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/support"
)

// Test that "dm reboot-bootloader" will reboot the system.  On a real system, "reboot-bootloader"
// will reboot to the bootloader.  However, in this test environment it will simply reboot and the
// system will end up back where it started.
func TestDmRebootBootloader(t *testing.T) {
	support.RebootWithCommand(t, "dm reboot-bootloader")
}
