package main

import (
	"testing"

	"fuchsia.googlesource.com/tests/bootserver"
)

func TestInitPartitionTables(t *testing.T) {
	logPattern := []bootserver.LogMatch{
		{"Received request from ", true},
		{"Proceeding with nodename ", true},
		{"Transfer starts", true},
		{"Transfer ends successfully", true},
		{"Issued reboot command to", false},
	}

	bootserver.CmdSearchLog(
		t, logPattern,
		bootserver.ToolPath(t, "bootserver"), "-n", bootserver.DefaultNodename,
		"--init-partition-tables", "/dev/class/block/000", "-1", "--fail-fast")
}

func TestMain(m *testing.M) {
	bootserver.RunTests(m, "netsvc.all-features=true, netsvc.netboot=true")
}
