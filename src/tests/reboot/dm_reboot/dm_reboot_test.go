package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/support"
)

// Test that "dm reboot" will reboot the system.
func TestDmReboot(t *testing.T) {
	support.RebootWithCommand(t, "dm reboot")
}
