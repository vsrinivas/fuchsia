// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package targets

import (
	"context"

	"go.fuchsia.dev/fuchsia/tools/qemu"
)

const (
	// aemuBinaryName is the name of the AEMU binary.
	aemuBinaryName = "emulator"
)

// AEMUTarget is a AEMU target.
type AEMUTarget struct {
	QEMUTarget
}

// NewAEMUTarget returns a new AEMU target with a given configuration.
func NewAEMUTarget(ctx context.Context, config QEMUConfig, opts Options) (*AEMUTarget, error) {
	target, err := NewQEMUTarget(ctx, config, opts)
	if err != nil {
		return nil, err
	}

	target.binary = aemuBinaryName
	target.builder = qemu.NewAEMUCommandBuilder()
	target.isQEMU = false

	return &AEMUTarget{QEMUTarget: *target}, nil
}
