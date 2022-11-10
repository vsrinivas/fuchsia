// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package build

type BootTest struct {
	Name string `json:"name"`

	// Label is the label of the test's GN target.
	Label string `json:"label"`

	// Path is the path to the test's file within the build directory.
	Path string `json:"path"`

	// The list of device types that this test should be run on.
	DeviceTypes []string `json:"device_types"`

	// QEMUKernelLabel points to an override of the standard QEMU kernel.
	QEMUKernelLabel string `json:"qemu_kernel_label"`
}
