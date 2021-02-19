// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

// TODO(fxbug.dev/65212): Remove this file after the typed channel migration.

// WireNoTypedChannels returns the LLCPP declaration when the relevant
// API would like to opt out of typed channels.
// TODO(fxbug.dev/65212): Uses of this method should be be replaced by WireDecl.
func (t *Type) WireNoTypedChannels() TypeVariant {
	if t.Kind == TypeKinds.Protocol || t.Kind == TypeKinds.Request {
		return TypeVariant("::zx::channel")
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
	for _, p := range m.Request {
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
func (p *Protocol) ShouldEmitTypedChannelCascadingInheritance() bool {
	// Note: using the "natural" domain object name, so the migration
	// to the wire namespace would not interfere with this check.
	if !rawChannelInterfaceAllowed[p.DeclName.Wire.String()] {
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
	"::llcpp::fuchsia::blobfs::Blobfs":                                  true,
	"::llcpp::fuchsia::device::instancelifecycle::test::InstanceDevice": true,
	"::llcpp::fuchsia::device::instancelifecycle::test::TestDevice":     true,
	"::llcpp::fuchsia::device::lifecycle::test::TestDevice":             true,
	"::llcpp::fuchsia::device::manager::Coordinator":                    true,
	"::llcpp::fuchsia::device::manager::DevhostController":              true,
	"::llcpp::fuchsia::driver::framework::DriverHost":                   true,
	"::llcpp::fuchsia::examples::EchoLauncher":                          true,
	"::llcpp::fuchsia::fshost::Registry":                                true,
	"::llcpp::fuchsia::gpu::magma::Primary":                             true,
	"::llcpp::fuchsia::hardware::audio::StreamConfig":                   true,
	"::llcpp::fuchsia::hardware::camera::Device":                        true,
	"::llcpp::fuchsia::hardware::display::Controller":                   true,
	"::llcpp::fuchsia::hardware::display::Provider":                     true,
	"::llcpp::fuchsia::hardware::goldfish::AddressSpaceDevice":          true,
	"::llcpp::fuchsia::hardware::goldfish::PipeDevice":                  true,
	"::llcpp::fuchsia::hardware::goldfish::SyncDevice":                  true,
	"::llcpp::fuchsia::hardware::input::Device":                         true,
	"::llcpp::fuchsia::hardware::network::Device":                       true,
	"::llcpp::fuchsia::hardware::network::DeviceInstance":               true,
	"::llcpp::fuchsia::hardware::pty::Device":                           true,
	"::llcpp::fuchsia::hardware::serial::NewDeviceProxy":                true,
	"::llcpp::fuchsia::hardware::tee::DeviceConnector":                  true,
	"::llcpp::fuchsia::hardware::telephony::transport::Qmi":             true,
	"::llcpp::fuchsia::hardware::usb::peripheral::Device":               true,
	"::llcpp::fuchsia::hardware::virtioconsole::Device":                 true,
	"::llcpp::fuchsia::input::report::InputDevice":                      true,
	"::llcpp::fuchsia::ldsvc::Loader":                                   true,
	"::llcpp::fuchsia::io::File":                                        true,
	"::llcpp::fuchsia::io::Directory":                                   true,
	"::llcpp::fuchsia::io::Node":                                        true,
	"::llcpp::fuchsia::io2::Directory":                                  true,
	"::llcpp::fuchsia::paver::DynamicDataSink":                          true,
	"::llcpp::fuchsia::paver::Paver":                                    true,
	"::llcpp::fuchsia::sysmem::Allocator":                               true,
	"::llcpp::fuchsia::sysmem::BufferCollection":                        true,
	"::llcpp::fuchsia::sysmem::BufferCollectionToken":                   true,
	"::llcpp::fuchsia::virtualconsole::SessionManager":                  true,
	"::llcpp::fuchsia::wlan::device::Connector":                         true,
	"::llcpp::fuchsia::wlan::tap::WlantapCtl":                           true,
	"::llcpp::fuchsia::driver::framework::Node":                         true,
	"::llcpp::fuchsia::paver::DataSink":                                 true,
	"::llcpp::fuchsia::power::manager::DriverManagerRegistration":       true,
}
