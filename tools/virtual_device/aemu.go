// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package virtual_device

import (
	"go.fuchsia.dev/fuchsia/tools/build"
	"go.fuchsia.dev/fuchsia/tools/qemu"
	fvdpb "go.fuchsia.dev/fuchsia/tools/virtual_device/proto"
)

// AEMUCommand sets options to run Fuchsia in AEMU on the given AEMUCommandBuilder.
//
// This returns an error if `Validate(fvd, images)` returns an error.
func AEMUCommand(b *qemu.AEMUCommandBuilder, fvd *fvdpb.VirtualDevice, images build.ImageManifest) error {
	if err := QEMUCommand(&b.QEMUCommandBuilder, fvd, images); err != nil {
		return err
	}
	if fvd.Hw.EnableKvm {
		b.SetFeature("KVM")
	}
	return nil
}
