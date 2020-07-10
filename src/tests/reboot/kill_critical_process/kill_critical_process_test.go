package main

import (
	"testing"

	"go.fuchsia.dev/fuchsia/src/tests/reboot/support"
)

// Test that "killall bootsvc" will reboot the system (because bootsvc is marked as a critical
// process; see also |zx_job_set_critical|).
func TestKillCriticalProcess(t *testing.T) {
	// Killing a critical process will result in an "unclean reboot" because, among other
	// things, the filesystem won't be shutdown cleanly.
	support.RebootWithCommand(t, "killall bootsvc", support.UncleanReboot)
}
