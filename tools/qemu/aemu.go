// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
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

	a.SetGPU("swiftshader_indirect")

	// headless
	a.SetAEMUFlag("-no-window")

	a.SetFlag("-vga", "none")
	a.SetFlag("-device", "virtio-keyboard-pci")
	a.SetFlag("-device", "virtio_input_multi_touch_pci_1")
	// End defaults
	return a
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

func (a *AEMUCommandBuilder) SetTarget(target Target, kvm bool) error {
	if kvm {
		a.SetFeature("KVM")
	}
	return a.QEMUCommandBuilder.SetTarget(target, kvm)
}

func (a *AEMUCommandBuilder) Build() ([]string, error) {
	if err := a.validate(); err != nil {
		return []string{}, err
	}
	cmd := []string{
		a.qemuPath,
	}

	if len(a.features) > 0 {
		cmd = append(cmd, "-feature")
		sort.Sort(sort.StringSlice(a.features))
		cmd = append(cmd, strings.Join(a.features, ","))
	}

	cmd = append(cmd, a.aemuArgs...)

	cmd = append(cmd, "-fuchsia")
	cmd = append(cmd, "-kernel")
	cmd = append(cmd, a.kernel)
	cmd = append(cmd, "-initrd")
	cmd = append(cmd, a.initrd)

	cmd = append(cmd, a.args...)

	// Treat the absense of specified networks as a directive to disable networking entirely.
	if !a.hasNetwork {
		cmd = append(cmd, "-net", "none")
	}

	if len(a.kernelArgs) > 0 {
		sort.Sort(sort.StringSlice(a.kernelArgs))
		cmd = append(cmd, "-append", strings.Join(a.kernelArgs, " "))
	}

	return cmd, nil
}
