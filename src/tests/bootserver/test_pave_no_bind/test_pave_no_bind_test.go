package main

import (
	"testing"

	"fuchsia.googlesource.com/tests/bootserver"
)

func TestPaveNoBind(t *testing.T) {
	_, cleanup := bootserver.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=true", "full")
	defer cleanup()

	// Test that advertise request is serviced and paving starts as netsvc.netboot=true
	bootserver.AttemptPaveNoBind(t, true)
}
