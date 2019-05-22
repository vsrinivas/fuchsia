package metrics

import (
	"testing"
)

// Cobalt logging has been disabled. This is a smoke test to ensure
// that invoking the log methods does not crash.
func TestNoOpLogging(t *testing.T) {
	logString(42, "42")
	logElapsedTime(42, 1, 2, "")
}
