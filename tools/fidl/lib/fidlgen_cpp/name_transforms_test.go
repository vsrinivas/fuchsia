// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Define equality of name types for testing purposes.
func (n name) Equal(o name) bool {
	return n.String() == o.String()
}

// Helper to parse compound identifiers
func parseIdent(s string) fidlgen.CompoundIdentifier {
	return fidlgen.EncodedCompoundIdentifier(s).Parse()
}

func TestChangeIfReserved(t *testing.T) {
	ctx := structMemberContext.NameContext
	assertEqual(t, changeIfReserved("not_reserved", ctx), "not_reserved")
	assertEqual(t, changeIfReserved("foobar", ctx), "foobar")

	// C++ keyword
	assertEqual(t, changeIfReserved("switch", ctx), "switch_")

	// Prevalent C constants
	assertEqual(t, changeIfReserved("EPERM", ctx), "EPERM_")

	// Bindings API
	assertEqual(t, changeIfReserved("Unknown", ctx), "Unknown_")
}

func TestBitsMemberContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		bitsMemberContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("foo"),
			Unified: makeName("kFoo"),
			Wire:    makeName("kFoo"),
		})

	assertEqual(t,
		bitsMemberContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("FOO_BAR"),
			Unified: makeName("kFooBar"),
			Wire:    makeName("kFooBar"),
		})

	assertEqual(t,
		bitsMemberContext.transform(fidlgen.Identifier("kFoo")),
		nameVariants{
			HLCPP:   makeName("kFoo"),
			Unified: makeName("kFoo"),
			Wire:    makeName("kFoo"),
		})

	assertEqual(t,
		bitsMemberContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("switch_"),
			Unified: makeName("kSwitch"),
			Wire:    makeName("kSwitch"),
		})

	assertEqual(t,
		bitsMemberContext.transform(fidlgen.Identifier("mask")),
		nameVariants{
			HLCPP:   makeName("mask"),
			Unified: makeName("kMask_"),
			Wire:    makeName("kMask_"),
		})
}

func TestEnumMemberContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		enumMemberContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("foo"),
			Unified: makeName("kFoo"),
			Wire:    makeName("kFoo"),
		})

	assertEqual(t,
		enumMemberContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("FOO_BAR"),
			Unified: makeName("kFooBar"),
			Wire:    makeName("kFooBar"),
		})

	assertEqual(t,
		enumMemberContext.transform(fidlgen.Identifier("kFoo")),
		nameVariants{
			HLCPP:   makeName("kFoo"),
			Unified: makeName("kFoo"),
			Wire:    makeName("kFoo"),
		})

	assertEqual(t,
		enumMemberContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("switch_"),
			Unified: makeName("kSwitch"),
			Wire:    makeName("kSwitch"),
		})

	assertEqual(t,
		enumMemberContext.transform(fidlgen.Identifier("mask")),
		nameVariants{
			HLCPP:   makeName("mask"),
			Unified: makeName("kMask"),
			Wire:    makeName("kMask"),
		})
}

func TestStructMemberContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		structMemberContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("foo"),
			Unified: makeName("foo"),
			Wire:    makeName("foo"),
		})

	assertEqual(t,
		structMemberContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("FOO_BAR"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		structMemberContext.transform(fidlgen.Identifier("FooBar")),
		nameVariants{
			HLCPP:   makeName("FooBar"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		structMemberContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("switch_"),
			Unified: makeName("switch_"),
			Wire:    makeName("switch_"),
		})
}

func TestTableMemberContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		tableMemberContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("foo"),
			Unified: makeName("foo"),
			Wire:    makeName("foo"),
		})

	assertEqual(t,
		tableMemberContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("FOO_BAR"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		tableMemberContext.transform(fidlgen.Identifier("FooBar")),
		nameVariants{
			HLCPP:   makeName("FooBar"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		tableMemberContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("switch_"),
			Unified: makeName("switch_"),
			Wire:    makeName("switch_"),
		})
}

func TestUnionMemberContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		unionMemberContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("foo"),
			Unified: makeName("foo"),
			Wire:    makeName("foo"),
		})

	assertEqual(t,
		unionMemberContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("FOO_BAR"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		unionMemberContext.transform(fidlgen.Identifier("FooBar")),
		nameVariants{
			HLCPP:   makeName("FooBar"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		unionMemberContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("switch_"),
			Unified: makeName("switch_"),
			Wire:    makeName("switch_"),
		})
}
func TestUnionMemberTagContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		unionMemberTagContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("kFoo"),
			Unified: makeName("kFoo"),
			Wire:    makeName("kFoo"),
		})

	assertEqual(t,
		unionMemberTagContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("kFooBar"),
			Unified: makeName("kFooBar"),
			Wire:    makeName("kFooBar"),
		})

	assertEqual(t,
		unionMemberTagContext.transform(fidlgen.Identifier("FooBar")),
		nameVariants{
			HLCPP:   makeName("kFooBar"),
			Unified: makeName("kFooBar"),
			Wire:    makeName("kFooBar"),
		})

	assertEqual(t,
		unionMemberTagContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("kSwitch"),
			Unified: makeName("kSwitch"),
			Wire:    makeName("kSwitch"),
		})
}

func TestMethodNameContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		methodNameContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("foo"),
			Unified: makeName("Foo"),
			Wire:    makeName("Foo"),
		})

	assertEqual(t,
		methodNameContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("FOO_BAR"),
			Unified: makeName("FooBar"),
			Wire:    makeName("FooBar"),
		})

	assertEqual(t,
		methodNameContext.transform(fidlgen.Identifier("FooBar")),
		nameVariants{
			HLCPP:   makeName("FooBar"),
			Unified: makeName("FooBar"),
			Wire:    makeName("FooBar"),
		})

	assertEqual(t,
		methodNameContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("switch_"),
			Unified: makeName("Switch"),
			Wire:    makeName("Switch"),
		})
}

func TestServiceMemberContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		serviceMemberContext.transform(fidlgen.Identifier("foo")),
		nameVariants{
			HLCPP:   makeName("foo"),
			Unified: makeName("foo"),
			Wire:    makeName("foo"),
		})

	assertEqual(t,
		serviceMemberContext.transform(fidlgen.Identifier("FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("FOO_BAR"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		serviceMemberContext.transform(fidlgen.Identifier("FooBar")),
		nameVariants{
			HLCPP:   makeName("FooBar"),
			Unified: makeName("foo_bar"),
			Wire:    makeName("foo_bar"),
		})

	assertEqual(t,
		serviceMemberContext.transform(fidlgen.Identifier("switch")),
		nameVariants{
			HLCPP:   makeName("switch_"),
			Unified: makeName("switch_"),
			Wire:    makeName("switch_"),
		})
}

func TestConstantContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		constantContext.transform(parseIdent("fidl.test/foo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::foo"),
			Unified: makeName("fidl_test::foo"),
			Wire:    makeName("fidl_test::wire::kFoo"),
		})

	assertEqual(t,
		constantContext.transform(parseIdent("fidl.test/FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("fidl::test::FOO_BAR"),
			Unified: makeName("fidl_test::FOO_BAR"),
			Wire:    makeName("fidl_test::wire::kFooBar"),
		})

	assertEqual(t,
		constantContext.transform(parseIdent("fidl.test/kFoo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::kFoo"),
			Unified: makeName("fidl_test::kFoo"),
			Wire:    makeName("fidl_test::wire::kFoo"),
		})

	assertEqual(t,
		constantContext.transform(parseIdent("fidl.test/switch")),
		nameVariants{
			HLCPP:   makeName("fidl::test::switch_"),
			Unified: makeName("fidl_test::switch_"),
			Wire:    makeName("fidl_test::wire::kSwitch"),
		})
}

func TestTypeContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		typeContext.transform(parseIdent("fidl.test/foo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::foo"),
			Unified: makeName("fidl_test::foo"),
			Wire:    makeName("fidl_test::wire::Foo"),
		})

	assertEqual(t,
		typeContext.transform(parseIdent("fidl.test/Foo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::Foo"),
			Unified: makeName("fidl_test::Foo"),
			Wire:    makeName("fidl_test::wire::Foo"),
		})

	assertEqual(t,
		typeContext.transform(parseIdent("fidl.test/FidlType")),
		nameVariants{
			HLCPP:   makeName("fidl::test::FidlType_"),
			Unified: makeName("fidl_test::FidlType_"),
			Wire:    makeName("fidl_test::wire::FidlType_"),
		})

	assertEqual(t,
		typeContext.transform(parseIdent("fidl.test/FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("fidl::test::FOO_BAR"),
			Unified: makeName("fidl_test::FOO_BAR"),
			Wire:    makeName("fidl_test::wire::FooBar"),
		})

	assertEqual(t,
		typeContext.transform(parseIdent("fidl.test/switch")),
		nameVariants{
			HLCPP:   makeName("fidl::test::switch_"),
			Unified: makeName("fidl_test::switch_"),
			Wire:    makeName("fidl_test::wire::Switch"),
		})
}

func TestServiceContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		serviceContext.transform(parseIdent("fidl.test/foo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::foo"),
			Unified: makeName("fidl::test::foo"),
			Wire:    makeName("fidl_test::Foo"),
		})

	assertEqual(t,
		serviceContext.transform(parseIdent("fidl.test/Foo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::Foo"),
			Unified: makeName("fidl::test::Foo"),
			Wire:    makeName("fidl_test::Foo"),
		})

	assertEqual(t,
		serviceContext.transform(parseIdent("fidl.test/FidlType")),
		nameVariants{
			HLCPP:   makeName("fidl::test::FidlType_"),
			Unified: makeName("fidl::test::FidlType_"),
			Wire:    makeName("fidl_test::FidlType_"),
		})

	assertEqual(t,
		serviceContext.transform(parseIdent("fidl.test/FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("fidl::test::FOO_BAR"),
			Unified: makeName("fidl::test::FOO_BAR"),
			Wire:    makeName("fidl_test::FooBar"),
		})

	assertEqual(t,
		serviceContext.transform(parseIdent("fidl.test/switch")),
		nameVariants{
			HLCPP:   makeName("fidl::test::switch_"),
			Unified: makeName("fidl::test::switch_"),
			Wire:    makeName("fidl_test::Switch"),
		})
}

func TestProtocolContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t,
		protocolContext.transform(parseIdent("fidl.test/foo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::foo"),
			Unified: makeName("fidl_test::Foo"),
			Wire:    makeName("fidl_test::Foo"),
		})

	assertEqual(t,
		protocolContext.transform(parseIdent("fidl.test/Foo")),
		nameVariants{
			HLCPP:   makeName("fidl::test::Foo"),
			Unified: makeName("fidl_test::Foo"),
			Wire:    makeName("fidl_test::Foo"),
		})

	assertEqual(t,
		protocolContext.transform(parseIdent("fidl.test/FidlType")),
		nameVariants{
			HLCPP:   makeName("fidl::test::FidlType_"),
			Unified: makeName("fidl_test::FidlType_"),
			Wire:    makeName("fidl_test::FidlType_"),
		})

	assertEqual(t,
		protocolContext.transform(parseIdent("fidl.test/FOO_BAR")),
		nameVariants{
			HLCPP:   makeName("fidl::test::FOO_BAR"),
			Unified: makeName("fidl_test::FooBar"),
			Wire:    makeName("fidl_test::FooBar"),
		})

	assertEqual(t,
		protocolContext.transform(parseIdent("fidl.test/switch")),
		nameVariants{
			HLCPP:   makeName("fidl::test::switch_"),
			Unified: makeName("fidl_test::Switch"),
			Wire:    makeName("fidl_test::Switch"),
		})
}

func TestNsComponentContext(t *testing.T) {
	// allow nameVariant.String() to work:
	currentVariant = testingVariant

	assertEqual(t, changeIfReserved("foo", nsComponentContext), "foo")
	assertEqual(t, changeIfReserved("using", nsComponentContext), "using_")
}
