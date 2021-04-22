// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

// TODO(fxbug.dev/65212): Remove this file after the typed channel migration.

// WireNoTypedChannels returns the LLCPP declaration when the relevant
// API would like to opt out of typed channels.
// TODO(fxbug.dev/65212): Uses of this method should be be replaced by WireDecl.
func (t *Type) WireNoTypedChannels() name {
	if t.Kind == TypeKinds.Protocol || t.Kind == TypeKinds.Request {
		return makeName("zx::channel")
	}
	return t.Wire
}

// ShouldEmitTypedChannelCascadingInheritance determines if the code generator
// should emit two overloads in the server interface API for this method, one
// with typed channels and the other with regular Zircon channels, during the
// migration to typed channels in LLCPP.
// TODO(fxbug.dev/65212): We should always only generate the version with typed
// channels.
func (m *Method) ShouldEmitTypedChannelCascadingInheritance() bool {
	for _, p := range m.RequestArgs {
		if p.Type.Kind == TypeKinds.Protocol || p.Type.Kind == TypeKinds.Request {
			return true
		}
	}
	return false
}

// ShouldEmitTypedChannelCascadingInheritance determines if the code generator
// should emit two versions of the server interface pure virtual class for this
// protocol, one with typed channels and the other with regular Zircon channels,
// during the migration to typed channels in LLCPP.
// TODO(fxbug.dev/65212): We should always only generate the version with typed
// channels.
func (p Protocol) ShouldEmitTypedChannelCascadingInheritance() bool {
	// Note: using the "natural" domain object name, so the migration
	// to the wire namespace would not interfere with this check.
	if !rawChannelInterfaceAllowed[p.nameVariants.Wire.String()] {
		return false
	}
	for _, m := range p.Methods {
		if m.ShouldEmitTypedChannelCascadingInheritance() {
			return true
		}
	}
	return false
}

// This is a list of FIDL protocols for which there are users of the deprecated
// RawChannelInterface LLCPP generated class. As we migrate code to typed
// channels, this list should shrink down to zero.
// TODO(fxbug.dev/65212): We should remove all the entries here.
var rawChannelInterfaceAllowed = map[string]bool{
	"::fuchsia_device_manager::Coordinator":              true,
	"::fuchsia_device_manager::DevhostController":        true,
	"::fuchsia_gpu_magma::Primary":                       true,
	"::fuchsia_hardware_display::Controller":             true,
	"::fuchsia_hardware_display::Provider":               true,
	"::fuchsia_hardware_goldfish::AddressSpaceDevice":    true,
	"::fuchsia_hardware_goldfish::PipeDevice":            true,
	"::fuchsia_hardware_goldfish::SyncDevice":            true,
	"::fuchsia_hardware_input::Device":                   true,
	"::fuchsia_hardware_usb_peripheral::Device":          true,
	"::fuchsia_hardware_virtioconsole::Device":           true,
	"::fuchsia_input_report::InputDevice":                true,
	"::fuchsia_io::Directory":                            true,
	"::fuchsia_io::File":                                 true,
	"::fuchsia_io::Node":                                 true,
	"::fuchsia_io2::Directory":                           true,
	"::fuchsia_paver::DataSink":                          true,
	"::fuchsia_paver::DynamicDataSink":                   true,
	"::fuchsia_paver::Paver":                             true,
	"::fuchsia_power_manager::DriverManagerRegistration": true,
	"::fuchsia_sysmem::Allocator":                        true,
	"::fuchsia_wlan_device::Connector":                   true,
	"::fuchsia_wlan_tap::WlantapCtl":                     true,
}
