// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

DefinitionBlock("", "DSDT", 2, "ZIRCON", "TEST", 0x01) {

	Scope(_SB) {}

	// Define a device that is a child of a scope, rather than a device.
	Scope(_GPE)
	{
		Device (DEV0) {
			Name (_HID, "TEST0000")

		}
	}

}
