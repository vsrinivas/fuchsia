// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgentest"
)

func TestCompileTypeNames(t *testing.T) {
	root := compile(fidlgentest.EndToEndTest{T: t}.Single(`
library foo.bar;

type U = union {
	1: a uint32;
};

protocol P {
	M(resource struct { a array<U:optional, 3>; b vector<client_end:<P, optional>>; });
};
`), HeaderOptions{})
	p := onlyProtocol(t, root)
	assertEqual(t, len(p.Methods), 1)
	m := p.Methods[0]
	assertEqual(t, len(m.RequestArgs), 2)

	ty := m.RequestArgs[0].Type
	expectEqual(t, ty.Natural.String(), "::std::array<::std::unique_ptr<::foo::bar::U>, 3>")
	expectEqual(t, ty.Wire.String(), "::fidl::Array<::foo_bar::wire::U, 3>")
	expectEqual(t, ty.Unified.String(), "::std::array<::std::unique_ptr<::foo_bar::U>, 3>")

	ty = m.RequestArgs[1].Type
	expectEqual(t, ty.Natural.String(), "::std::vector<::fidl::InterfaceHandle<::foo::bar::P>>")
	expectEqual(t, ty.Wire.String(), "::fidl::VectorView<::fidl::ClientEnd<::foo_bar::P>>")
	// TODO(fxbug.dev/72980): Switch to ClientEnd/ServerEnd and underscore namespace when
	// corresponding endpoint types can easily convert into each other.
	expectEqual(t, ty.Unified.String(), "::std::vector<::fidl::InterfaceHandle<::foo::bar::P>>")
}
