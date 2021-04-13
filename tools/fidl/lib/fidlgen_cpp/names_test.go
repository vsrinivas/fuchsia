// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"
)

func TestName(t *testing.T) {
	ns := namespace([]string{"foo", "bar"})
	v := ns.member("Baz")
	assertEqual(t, v.String(), "::foo::bar::Baz")

	p := v.prependName("Prefix")
	assertEqual(t, p.String(), "::foo::bar::PrefixBaz")

	s := v.appendName("Suffix")
	assertEqual(t, s.String(), "::foo::bar::BazSuffix")

	n := v.nest("Quux")
	assertEqual(t, n.String(), "::foo::bar::Baz::Quux")
	assertEqual(t, n.Self(), "Quux")

	tmpl := v.template(namespace([]string{"hello"}).member("World"))
	assertEqual(t, tmpl.String(), "::foo::bar::Baz<::hello::World>")
	assertEqual(t, tmpl.Self(), "Baz")

	tmpl_nest := tmpl.nest("Inner")
	assertEqual(t, tmpl_nest.String(), "::foo::bar::Baz<::hello::World>::Inner")

}
