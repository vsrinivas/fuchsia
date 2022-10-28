// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package emulator

import (
	"context"
	"fmt"
	"path/filepath"

	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/util"
	"go.fuchsia.dev/fuchsia/tools/emulator"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
	"go.uber.org/multierr"
)

// QemuInstance is a live instance of Fuchsia running in QEMU.
type QemuInstance struct {
	nodename string
	*emulator.Instance
}

// QemuInstanceArgs is the set of arguments required for creating a QemuInstance.
type QemuInstanceArgs struct {
	// The nodename that should be assigned to the QEMU instance.
	Nodename string
	// The name of the initrd from images.json that should be used.
	Initrd string
	// The root_out_dir for the host toolchain, e.g. /.../out/default/host_x64
	HostX64Path string
	// The filepath of the authorized_keys file that should be provisioned with the QEMU
	// instance. If unset, defaults to the fuchsia checkout's authorized_keys file
	// (//.ssh/authorized_keys).
	HostPathAuthorizedKeys string
	// The network devices that should be added to the QEMU instance, i.e. any tap interfaces
	// that it should be using.
	NetworkDevices []*fvdpb.Netdev
}

// The relative path from the root of the fuchsia checkout to this file. This is used to namespace
// host_test_data used by this library to avoid collisions.
const SourceRootRelativeDir = "src/connectivity/network/testing/conformance/emulator"

// The path relative to $host_out_dir to the "test_data" directory in which
// //tools/emulator:archive-qemu places the qemu distribution.
const HostPathTestDataDirForQemuDistro = "test_data"

// NewQemuInstance creates a new QemuInstance according to the provided args. The instance is
// terminated on ctx.Done.
func NewQemuInstance(
	ctx context.Context,
	args QemuInstanceArgs,
) (q *QemuInstance, err error) {
	distro, err := emulator.UnpackFrom(
		filepath.Join(args.HostX64Path, HostPathTestDataDirForQemuDistro),
		emulator.DistributionParams{
			Emulator: emulator.Qemu,
		},
	)
	if err != nil {
		return nil, fmt.Errorf("couldn't unpack emulator distribution: %w", err)
	}

	// We don't need the distro after we've created and started the emulator Instance.
	defer multierr.AppendInvoke(&err, multierr.Invoke(distro.Delete))

	arch, err := distro.TargetCPU()
	if err != nil {
		return nil, fmt.Errorf("couldn't determine build target CPU: %w", err)
	}

	device := emulator.DefaultVirtualDevice(string(arch))
	device.Initrd = args.Initrd
	device.KernelArgs = append(device.KernelArgs,
		fmt.Sprintf("zircon.nodename=%s", args.Nodename))
	device.Hw.NetworkDevices = args.NetworkDevices

	hostTestDataDir := filepath.Join(
		args.HostX64Path,
		SourceRootRelativeDir,
	)

	authorizedKeysPath := args.HostPathAuthorizedKeys

	if authorizedKeysPath == "" {
		defaultAuthorizedKeysPath, err := util.DutAuthorizedKeysPath()
		if err != nil {
			return nil, fmt.Errorf("couldn't generate dut authorized keys path: %w", err)
		}
		authorizedKeysPath = defaultAuthorizedKeysPath
	}

	i, err := distro.CreateContextWithAuthorizedKeys(
		ctx,
		device,
		filepath.Join(hostTestDataDir, "zbi"),
		authorizedKeysPath,
	)
	if err != nil {
		return nil, fmt.Errorf("couldn't create emulator instance: %w", err)
	}

	if err := i.Start(); err != nil {
		return nil, fmt.Errorf("couldn't start emulator instance; %w", err)
	}

	return &QemuInstance{
		nodename: args.Nodename,
		Instance: i,
	}, nil
}
