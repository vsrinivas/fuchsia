// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"go.fuchsia.dev/fuchsia/tools/qemu"
)

const (
	// aemuBinaryName is the name of the AEMU binary.
	aemuBinaryName = "emulator"
)

var _ Target = (*AEMUTarget)(nil)

// AEMUTarget is a AEMU target.
type AEMUTarget struct {
	QEMUTarget
}

// NewAEMUTarget returns a new AEMU target with a given configuration.
func NewAEMUTarget(config QEMUConfig, opts Options) (*AEMUTarget, error) {
	target, err := NewQEMUTarget(config, opts)
	if err != nil {
		return nil, err
	}

	target.binary = aemuBinaryName
	target.builder = qemu.NewAEMUCommandBuilder()

	return &AEMUTarget{QEMUTarget: *target}, nil
}
