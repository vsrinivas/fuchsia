package main

import (
	"testing"

	"fuchsia.googlesource.com/tests/bootserver"
)

func TestPaveNoBind(t *testing.T) {
	// Test that advertise request is serviced and paving starts as netsvc.netboot=true
	bootserver.AttemptPaveNoBind(t, true)
}

func TestMain(m *testing.M) {
	bootserver.RunTests(m, "netsvc.all-features=true, netsvc.netboot=true")
}
