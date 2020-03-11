// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package qemu

import (
	"strings"
)

type AEMUCommandBuilder struct {
	QEMUCommandBuilder

	aemuArgs []string
	features []string
}

func (a *AEMUCommandBuilder) SetFeature(feature string) {
	a.features = append(a.features, feature)
}

func (a *AEMUCommandBuilder) SetAEMUFlag(args ...string) {
	a.aemuArgs = append(a.aemuArgs, args...)
}

func (a *AEMUCommandBuilder) SetGPU(gpu string) {
	a.SetFeature("VULKAN")
	a.SetAEMUFlag("-gpu", gpu)
}

func (a *AEMUCommandBuilder) SetTarget(target Target, kvm bool) {
	if kvm {
		a.SetFeature("GLDirectMem")
		a.SetFeature("KVM")
	} else {
		a.SetFeature("-GLDirectMem")
	}
	a.QEMUCommandBuilder.SetTarget(target, kvm)
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
		cmd = append(cmd, "-append", strings.Join(a.kernelArgs, " "))
	}

	return cmd, nil
}
