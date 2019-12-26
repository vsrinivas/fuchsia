// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"go.fuchsia.dev/fuchsia/tools/bootserver/lib"
)

// Options represents lifecycle options for a target. The options will not necessarily make
// sense for all target types.
type Options struct {
	// Netboot gives whether to netboot or pave. Netboot here is being used in the
	// colloquial sense of only sending netsvc a kernel to mexec. If false, the target
	// will be paved. Ignored for QEMUTarget.
	Netboot bool

	// SSHKey is a private SSH key file, corresponding to an authorized key to be paved or
	// to one baked into a boot image.
	SSHKey string

	// PaveImgs are a list of images to be passed into BootZedbootShim().
	// TODO(fxbug.dev/38517): remove this once BootZedbootShim is deprecated.
	PaveImgs []bootserver.Image
}
