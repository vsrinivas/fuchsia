// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"
)

func TestName(t *testing.T) {
	ns := Namespace([]string{"foo", "bar"})
	v := ns.Member("Baz")
	assertEqual(t, v.String(), "::foo::bar::Baz")

	p := v.PrependName("Prefix")
	assertEqual(t, p.String(), "::foo::bar::PrefixBaz")

	s := v.AppendName("Suffix")
	assertEqual(t, s.String(), "::foo::bar::BazSuffix")

	n := v.Nest("Quux")
	assertEqual(t, n.String(), "::foo::bar::Baz::Quux")
	assertEqual(t, n.Self(), "Quux")

	tmpl := v.Template(Namespace([]string{"hello"}).Member("World"))
	assertEqual(t, tmpl.String(), "::foo::bar::Baz<::hello::World>")
	assertEqual(t, tmpl.Self(), "Baz")

	tmpl_nest := tmpl.Nest("Inner")
	assertEqual(t, tmpl_nest.String(), "::foo::bar::Baz<::hello::World>::Inner")

}
