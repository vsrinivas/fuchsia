// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ADDING A NEW PROTOCOL
// When adding a new protocol, add a macro call at the end of this file after
// the last protocol definition with a tag, value, name, and flags in the form:
//
// DDK_PROTOCOL_DEF(tag, value, protocol_name)
//
// The value must be a unique identifier that is just the previous protocol
// value plus 1.

// clang-format off

#ifndef DDK_FIDL_PROTOCOL_DEF
#error Internal use only. Do not include.
#else
DDK_FIDL_PROTOCOL_DEF(RPMB,           1, "fuchsia.hardware.rpmb.Rpmb")
DDK_FIDL_PROTOCOL_DEF(CHROMEOS_EC,    2, "fuchsia.hardware.google.ec.Device")
DDK_FIDL_PROTOCOL_DEF(I2C,            3, "fuchsia.hardware.i2c.Device")
DDK_FIDL_PROTOCOL_DEF(PCI,            4, "fuchsia.hardware.pci.Device")
DDK_FIDL_PROTOCOL_DEF(GOLDFISH_PIPE,  5, "fuchsia.hardware.goldfish.pipe.GoldfishPipe")
DDK_FIDL_PROTOCOL_DEF(ADDRESS_SPACE,  6, "fuchsia.hardware.goldfish.AddressSpaceDevice")
DDK_FIDL_PROTOCOL_DEF(GOLDFISH_SYNC,  7, "fuchsia.hardware.goldfish.SyncDevice")
DDK_FIDL_PROTOCOL_DEF(SPI,            8, "fuchsia.hardware.spi.Device")
DDK_FIDL_PROTOCOL_DEF(SYSMEM,         9, "fuchsia.hardware.sysmem.Sysmem")
DDK_FIDL_PROTOCOL_DEF(AML_MAILBOX,    10, "fuchsia.hardware.mailbox.Device")
DDK_FIDL_PROTOCOL_DEF(PLATFORM_BUS,          11, "fuchsia.hardware.platform.bus.PlatformBus")
#undef DDK_FIDL_PROTOCOL_DEF
#endif
