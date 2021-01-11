// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

// TODO(fxbug.dev/65212): Remove this file after the typed channel migration.

// LLDeclNoTypedChannels returns the LLCPP declaration when the relevant
// API would like to opt out of typed channels.
// TODO(fxbug.dev/65212): Uses of this method should be be replaced by LLDecl.
func (t *Type) LLDeclNoTypedChannels() string {
	if t.Kind == TypeKinds.Protocol || t.Kind == TypeKinds.Request {
		return "::zx::channel"
	}
	return t.LLDecl
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
