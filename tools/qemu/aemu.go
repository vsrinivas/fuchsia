// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"fmt"
	"sort"
	"strings"
)

type AEMUCommandBuilder struct {
	QEMUCommandBuilder

	aemuArgs []string
	features []string
}

func NewAEMUCommandBuilder() *AEMUCommandBuilder {
	a := &AEMUCommandBuilder{}
	// Default values for AEMU
	a.SetFeature("GLDirectMem")
	a.SetFeature("VirtioInput")
	a.SetFeature("HostComposition")

	a.SetGPU("swiftshader_indirect")

	// headless
	a.SetAEMUFlag("-no-window")

	a.SetFlag("-vga", "none")
	a.SetFlag("-device", "virtio-keyboard-pci")
	a.SetFlag("-device", "virtio_input_multi_touch_pci_1")
	// End defaults
	return a
}

func (a *AEMUCommandBuilder) AddSerial(c Chardev) {
	args := []string{"stdio", fmt.Sprintf("id=%s", c.ID)}
	if c.Logfile != "" {
		args = append(args, fmt.Sprintf("logfile=%s", c.Logfile))
	}
	if !c.Signal {
		args = append(args, "signal=off")
	}
	args = append(args, "echo=off")
	a.SetFlag("-chardev", strings.Join(args, ","))
	a.SetFlag("-serial", fmt.Sprintf("chardev:%s", c.ID))
}

func (a *AEMUCommandBuilder) SetFeature(feature string) {
	a.features = append(a.features, feature)
}

func (a *AEMUCommandBuilder) SetAEMUFlag(args ...string) {
	a.aemuArgs = append(a.aemuArgs, args...)
}

func (a *AEMUCommandBuilder) SetGPU(gpu string) {
	a.SetFeature("Vulkan")
	a.SetAEMUFlag("-gpu", gpu)
}

func (a *AEMUCommandBuilder) SetTarget(target Target, kvm bool) {
	if kvm {
		a.SetFeature("KVM")
	}
	a.QEMUCommandBuilder.SetTarget(target, kvm)
}

func (a *AEMUCommandBuilder) Build() ([]string, error) {
	if a.qemuPath == "" {
		return []string{}, fmt.Errorf("QEMU binary path must be set.")
	}
	config, err := a.BuildConfig()
	if err != nil {
		return []string{}, err
	}

	cmd := []string{
		a.qemuPath,
	}
	if len(config.Features) > 0 {
		cmd = append(cmd, "-feature")
		sort.Sort(sort.StringSlice(config.Features))
		cmd = append(cmd, strings.Join(config.Features, ","))
	}

	cmd = append(cmd, config.Options...)

	cmd = append(cmd, "-fuchsia")
	cmd = append(cmd, config.Args...)

	if len(config.KernelArgs) > 0 {
		sort.Sort(sort.StringSlice(config.KernelArgs))
		cmd = append(cmd, "-append", strings.Join(config.KernelArgs, " "))
	}

	return cmd, nil
}

func (a *AEMUCommandBuilder) BuildConfig() (Config, error) {
	config, err := a.QEMUCommandBuilder.BuildConfig()
	if err != nil {
		return config, err
	}
	config.Features = a.features
	config.Options = a.aemuArgs
	return config, nil
}
