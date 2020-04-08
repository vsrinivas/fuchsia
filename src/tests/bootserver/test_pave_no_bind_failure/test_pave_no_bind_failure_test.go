package main

import (
	"testing"

	"fuchsia.googlesource.com/tests/bootserver"
)

func TestPaveNoBindFailure(t *testing.T) {
	// Test that advertise request is NOT serviced and paving does NOT start
	// as netsvc.netboot=false
	bootserver.AttemptPaveNoBind(t, false)
}

func TestMain(m *testing.M) {
	bootserver.RunTests(m, "netsvc.all-features=true, netsvc.netboot=false")
}
