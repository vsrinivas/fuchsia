package main

import (
	"testing"

	"fuchsia.googlesource.com/tests/bootserver"
)

func TestPaveNoBindFailure(t *testing.T) {
	_, cleanup := bootserver.StartQemu(t, "netsvc.all-features=true, netsvc.netboot=false", "full")
	defer cleanup()

	// Test that advertise request is NOT serviced and paving does NOT start
	// as netsvc.netboot=false
	bootserver.AttemptPaveNoBind(t, false)
}
