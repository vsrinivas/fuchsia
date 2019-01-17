// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"

	"fuchsia.googlesource.com/tools/build"
)

// Config gives a high-level configuration for QEMU on Fuchsia.
type Config struct {
	// QEMUBin is a path to the QEMU binary.
	QEMUBin string

	// CPU is the emulated CPU (e.g., "x64" or "arm64").
	CPU string

	// KVM gives whether to enable KVM.
	KVM bool

	// MinFSImage is the path to a minfs image. If unset, none will be attached.
	MinFSImage string

	// PCIAddr is the PCI address under which a minfs image will be mounted as a block device.
	PCIAddr string

	// InternetAccess gives whether to enable internet access.
	InternetAccess bool
}

// CreateInvocation creates a QEMU invocation given a particular configuration, a list of
// images, and any specified command-line arguments.
func CreateInvocation(cfg Config, imgs build.Images, cmdlineArgs []string) ([]string, error) {
	if _, err := os.Stat(cfg.QEMUBin); err != nil {
		return nil, fmt.Errorf("QEMU binary not found: %v", err)
	}
	absQEMUBinPath, err := filepath.Abs(cfg.QEMUBin)
	if err != nil {
		return nil, err
	}

	invocation := []string{absQEMUBinPath}
	addArgs := func(args ...string) {
		invocation = append(invocation, args...)
	}

	if cfg.CPU == "arm64" {
		if cfg.KVM {
			addArgs("-machine", "virt,gic_version=host")
			addArgs("-cpu", "host")
			addArgs("-enable-kvm")
		} else {
			addArgs("-machine", "virt,gic_version=3")
			addArgs("-machine", "virtualization=true")
			addArgs("-cpu", "cortex-a53")
		}
	} else if cfg.CPU == "x64" {
		addArgs("-machine", "q35")
		// Necessary for userboot.shutdown to trigger properly, since it writes to
		// 0xf4 to debug-exit in QEMU.
		addArgs("-device", "isa-debug-exit,iobase=0xf4,iosize=0x04")

		if cfg.KVM {
			addArgs("-cpu", "host")
			addArgs("-enable-kvm")
		} else {
			addArgs("-cpu", "Haswell,+smap,-check,-fsgsbase")
		}
	} else {
		return nil, fmt.Errorf("cpu %q not recognized", cfg.CPU)
	}

	addArgs("-m", "4096")
	addArgs("-smp", "4")
	addArgs("-nographic")
	addArgs("-serial", "stdio")
	addArgs("-monitor", "none")

	if !cfg.InternetAccess {
		addArgs("-net", "none")
	}

	if cfg.MinFSImage != "" {
		if cfg.PCIAddr == "" {
			return nil, errors.New("PCI address must be set if a MinFS image is provided")
		}
		absMinFSImage, err := filepath.Abs(cfg.MinFSImage)
		if err != nil {
			return nil, err
		}
		// Swarming hard-links Isolate downloads with a cache and the very same cached minfs
		// image will be used across multiple tasks. To ensure that it remains blank, we
		// must break its link.
		if err := overwriteFileWithCopy(absMinFSImage); err != nil {
			return nil, err
		}
		addArgs("-drive", fmt.Sprintf("file=%s,format=raw,if=none,id=testdisk", absMinFSImage))
		addArgs("-device", fmt.Sprintf("virtio-blk-pci,drive=testdisk,addr=%s", cfg.PCIAddr))
	}

	qemuKernel := imgs.Get("qemu-kernel")
	if qemuKernel == nil {
		return nil, fmt.Errorf("could not find qemu-kernel")
	}
	zirconA := imgs.Get("zircon-a")
	if zirconA == nil {
		return nil, fmt.Errorf("could not find zircon-a")
	}
	addArgs("-kernel", qemuKernel.Path)
	addArgs("-initrd", zirconA.Path)

	if storageFull := imgs.Get("storage-full"); storageFull != nil {
		addArgs("-drive", fmt.Sprintf("file=%s,format=raw,if=none,id=maindisk", storageFull.Path))
		addArgs("-device", "virtio-blk-pci,drive=maindisk")
	}

	addArgs("-append", strings.Join(cmdlineArgs, " "))
	return invocation, nil
}

func overwriteFileWithCopy(path string) error {
	copy, err := ioutil.TempFile("", "botanist")
	if err != nil {
		return err
	}
	if err = copyFile(path, copy.Name()); err != nil {
		return err
	}
	if err = os.Remove(path); err != nil {
		return err
	}
	return os.Rename(copy.Name(), path)
}

func copyFile(src, dest string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	info, err := in.Stat()
	if err != nil {
		return err
	}
	out, err := os.OpenFile(dest, os.O_WRONLY|os.O_CREATE, info.Mode().Perm())
	if err != nil {
		return err
	}
	defer out.Close()
	_, err = io.Copy(out, in)
	return err
}
