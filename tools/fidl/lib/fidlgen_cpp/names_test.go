// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"
)

func TestName(t *testing.T) {
	foo := name([]string{"Foo"})
	assertEqual(t, foo.String(), "Foo")

	foofoo := foo.Append("foo")
	assertEqual(t, foofoo.String(), "Foofoo")

	bar := foo.Nest("Bar")
	assertEqual(t, bar.String(), "Foo::Bar")
	assertEqual(t, bar.Unqualified(), "Bar")

	baz := bar.Append("Baz")
	assertEqual(t, baz.String(), "Foo::BarBaz")

	quux := baz.Prepend("Quux")
	assertEqual(t, quux.String(), "Foo::QuuxBarBaz")
}

func TestDeclVariant(t *testing.T) {
	v := NewDeclVariant("Baz", Namespace([]string{"foo", "bar"}))
	assertEqual(t, v.String(), "::foo::bar::Baz")

	p := v.PrependName("Prefix")
	assertEqual(t, p.String(), "::foo::bar::PrefixBaz")

	s := v.AppendName("Suffix")
	assertEqual(t, s.String(), "::foo::bar::BazSuffix")

	n := v.Nest("Quux")
	assertEqual(t, n.String(), "::foo::bar::Baz::Quux")
	assertEqual(t, n.Unqualified(), "Quux")

	tmpl := v.Template(NewDeclVariant("World", Namespace([]string{"hello"})))
	assertEqual(t, tmpl.String(), "::foo::bar::Baz<::hello::World>")
}
