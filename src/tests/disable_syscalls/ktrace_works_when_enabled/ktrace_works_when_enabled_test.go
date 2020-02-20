package main

import (
	"testing"

	"fuchsia.googlesource.com/testing/qemu"
	"fuchsia.googlesource.com/tests/disable_syscalls/support"
)

func TestKtraceWorksWhenEnabled(t *testing.T) {
	distro, err := qemu.Unpack()
	if err != nil {
		t.Fatal(err)
	}
	defer distro.Delete()
	arch, err := distro.TargetCPU()
	if err != nil {
		t.Fatal(err)
	}

	stdout, stderr, err := distro.RunNonInteractive(
		"/boot/bin/ktrace start 0xff",
		support.ToolPath(t, "minfs"),
		support.ToolPath(t, "zbi"),
		qemu.Params{
			Arch:          arch,
			ZBI:           support.ZbiPath(t),
			AppendCmdline: "kernel.enable-debugging-syscalls=true",
		})
	if err != nil {
		t.Fatal(err)
	}

	support.EnsureDoesNotContain(t, stdout, "ZX_ERR_NOT_SUPPORTED")
	support.EnsureDoesNotContain(t, stderr, "ZX_ERR_NOT_SUPPORTED")
}
