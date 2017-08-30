// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package core

import (
	"fidl/compiler/lexer"
	"testing"
)

func checkParent(scope *Scope, expectedParent *Scope, t *testing.T) {
	if scope.Parent() != expectedParent {
		t.Errorf("The parent of %v is %v, expecting %v", scope, scope.Parent(), expectedParent)
	}
}

// TestLookupType tests looking up an Enum using different names in different
// scopes.
func TestLookupType(t *testing.T) {
	// Create a new file scope.
	fileScope := NewTestFileScope("foo.bar")

	// Create a new interface scope
	interfaceScope := NewInterfaceScope(fileScope)

	// Obtain the auto-generated abstract module scopes.
	fooBarScope := fileScope.descriptor.abstractScopesByName["foo.bar"]
	fooScope := fileScope.descriptor.abstractScopesByName["foo"]
	rootScope := fileScope.descriptor.abstractScopesByName[""]

	// Check the parenting
	checkParent(interfaceScope, fileScope, t)
	checkParent(fileScope, fooBarScope, t)
	checkParent(fooBarScope, fooScope, t)
	checkParent(fooScope, rootScope, t)
	checkParent(rootScope, nil, t)

	// Create an Enum
	mojomEnum := NewTestEnum("MyEnum")
	mojomEnum.RegisterInScope(interfaceScope)

	if mojomEnum.FullyQualifiedName() != "foo.bar.MyInterface.MyEnum" {
		t.Errorf("mojomEnum.FullyQualifiedName()=%q", mojomEnum.FullyQualifiedName())
	}

	cases := []struct {
		// The name to lookup.
		lookupName string

		// The scope in which to start the lookup.
		scope *Scope

		// Do we expect success>
		expectSuccess bool
	}{
		// Lookup in interfaceScope
		{"MyEnum", interfaceScope, true},
		{"MyInterface.MyEnum", interfaceScope, true},
		{"bar.MyInterface.MyEnum", interfaceScope, true},
		{"foo.bar.MyInterface.MyEnum", interfaceScope, true},
		{"blah.foo.bar.MyInterface.MyEnum", interfaceScope, false},

		// Lookup in fileScope
		{"MyEnum", fileScope, false},
		{"MyInterface.MyEnum", fileScope, true},
		{"bar.MyInterface.MyEnum", fileScope, true},
		{"foo.bar.MyInterface.MyEnum", fileScope, true},
		{"blah.foo.bar.MyInterface.MyEnum", fileScope, false},

		// Lookup in foo.bar scope.  (This is artificial as we never directly
		// lookup in an abstract module scope.)
		{"MyEnum", fooBarScope, false},
		{"MyInterface.MyEnum", fooBarScope, true},
		{"bar.MyInterface.MyEnum", fooBarScope, true},
		{"foo.bar.MyInterface.MyEnum", fooBarScope, true},
		{"blah.foo.bar.MyInterface.MyEnum", fooBarScope, false},

		// Lookup in foo scope. (This is artificial as we never directly
		// lookup in an abstract module scope.)
		{"MyEnum", fooScope, false},
		{"MyInterface.MyEnum", fooScope, false},
		{"bar.MyInterface.MyEnum", fooScope, true},
		{"foo.bar.MyInterface.MyEnum", fooScope, true},
		{"blah.foo.bar.MyInterface.MyEnum", fooScope, false},

		// Lookup in root scope. (This is artificial as we never directly
		// lookup in an abstract module scope.)
		{"MyEnum", rootScope, false},
		{"MyInterface.MyEnum", rootScope, false},
		{"bar.MyInterface.MyEnum", rootScope, false},
		{"foo.bar.MyInterface.MyEnum", rootScope, true},
		{"blah.foo.bar.MyInterface.MyEnum", rootScope, false},
	}
	for _, c := range cases {
		lookupObject := c.scope.LookupObject(c.lookupName, LookupAcceptType)
		var got UserDefinedType
		if lookupObject != nil {
			got = lookupObject.(UserDefinedType)
		}
		if (got == mojomEnum) != c.expectSuccess {
			t.Errorf("c={%q, %s, %v}, got=%v", c.lookupName, c.scope.fullyQualifiedName, c.expectSuccess, got)
		}
	}
}

// TestLookupValue tests lookup up an enum value using different names
// in different scopes with different assignee types.
func TestLookupValue(t *testing.T) {
	// Create a new file scope.
	fileScope := NewTestFileScope("foo.bar")

	// Create a new interface scope
	interfaceScope := NewInterfaceScope(fileScope)

	// Obtain the auto-generated abstract module scopes.
	fooBarScope := fileScope.descriptor.abstractScopesByName["foo.bar"]
	fooScope := fileScope.descriptor.abstractScopesByName["foo"]
	rootScope := fileScope.descriptor.abstractScopesByName[""]

	// Create an Enum
	mojomEnum := NewTestEnum("MyEnum")
	mojomEnum.RegisterInScope(interfaceScope)

	// Create an EnumValue
	mojomEnum.InitAsScope(interfaceScope)
	mojomEnum.AddEnumValue(DeclTestData("TheValue"), nil)
	enumValue := mojomEnum.Values[0]

	if enumValue.FullyQualifiedName() != "foo.bar.MyInterface.MyEnum.TheValue" {
		t.Errorf("enumValue.FullyQualifiedName()=%q", mojomEnum.FullyQualifiedName())
	}

	// Create two typeRefs to act as the assignee types.
	enumTypeRef := NewResolvedUserTypeRef("look.an.enum", mojomEnum)
	unknownTypeRef := NewUserTypeRef("not.yet.resolved", false, false, nil, lexer.Token{})

	cases := []struct {
		// The name to lookup
		lookupName string

		// The scope in which to look first.
		scope *Scope

		// The assigneeType associated with the lookup.
		assigneeType TypeRef

		// Do we expect success.
		expectSuccess bool
	}{
		// Lookup in interfaceScope
		{"TheValue", interfaceScope, enumTypeRef, true},
		{"TheValue", interfaceScope, unknownTypeRef, false},
		{"MyEnum.TheValue", interfaceScope, enumTypeRef, true},
		{"MyInterface.MyEnum.TheValue", interfaceScope, enumTypeRef, true},
		{"bar.MyInterface.MyEnum.TheValue", interfaceScope, enumTypeRef, true},
		{"foo.bar.MyInterface.MyEnum.TheValue", interfaceScope, enumTypeRef, true},
		{"blah.foo.bar.MyInterface.MyEnum.TheValue", interfaceScope, enumTypeRef, false},

		// Lookup in fileScope
		{"TheValue", fileScope, enumTypeRef, true},
		{"TheValue", fileScope, unknownTypeRef, false},
		{"MyEnum.TheValue", fileScope, enumTypeRef, false},
		{"MyInterface.MyEnum.TheValue", fileScope, enumTypeRef, true},
		{"bar.MyInterface.MyEnum.TheValue", fileScope, enumTypeRef, true},
		{"foo.bar.MyInterface.MyEnum.TheValue", fileScope, enumTypeRef, true},
		{"blah.foo.bar.MyInterface.MyEnum.TheValue", fileScope, enumTypeRef, false},

		// Lookup in foo.bar scope (This is artificial as we never directly
		// lookup in an abstract module scope.)
		{"TheValue", fooBarScope, enumTypeRef, true},
		{"MyEnum.TheValue", fooBarScope, enumTypeRef, false},
		{"MyInterface.MyEnum.TheValue", fooBarScope, enumTypeRef, true},
		{"bar.MyInterface.MyEnum.TheValue", fooBarScope, enumTypeRef, true},
		{"foo.bar.MyInterface.MyEnum.TheValue", fooBarScope, enumTypeRef, true},
		{"blah.foo.bar.MyInterface.MyEnum.TheValue", fooBarScope, enumTypeRef, false},

		// Lookup in foo scope. (This is artificial as we never directly
		// lookup in an abstract module scope.)
		{"TheValue", fooScope, enumTypeRef, true},
		{"MyEnum.TheValue", fooScope, enumTypeRef, false},
		{"MyInterface.MyEnum.TheValue", fooScope, enumTypeRef, false},
		{"bar.MyInterface.MyEnum.TheValue", fooScope, enumTypeRef, true},
		{"foo.bar.MyInterface.MyEnum.TheValue", fooScope, enumTypeRef, true},
		{"blah.foo.bar.MyInterface.MyEnum.TheValue", fooScope, enumTypeRef, false},

		// Lookup in root scope.  (This is artificial as we never directly
		// lookup in an abstract module scope.)
		{"MyEnum.TheValue", rootScope, enumTypeRef, false},
		{"MyInterface.MyEnum.TheValue", rootScope, enumTypeRef, false},
		{"bar.MyInterface.MyEnum.TheValue", rootScope, enumTypeRef, false},
		{"foo.bar.MyInterface.MyEnum.TheValue", rootScope, enumTypeRef, true},
		{"blah.foo.bar.MyInterface.MyEnum.TheValue", rootScope, enumTypeRef, false},
	}
	for _, c := range cases {
		lookupObject := c.scope.LookupObject(c.lookupName, LookupAcceptValue)
		if lookupObject == nil {
			ref := NewUserValueRef(AssigneeSpec{Type: c.assigneeType},
				c.lookupName, c.scope, lexer.Token{})
			if resolveSpecialEnumValueAssignment(ref) {
				lookupObject = ref.resolvedDeclaredValue
			}
		}
		var got *EnumValue
		if lookupObject != nil {
			got = lookupObject.(*EnumValue)
		}
		if (got == enumValue) != c.expectSuccess {
			t.Errorf("c={lookupName=%q, scopeName=%s, assigneType=%v, expectSuccess=%v}, got=%v",
				c.lookupName, c.scope.fullyQualifiedName, c.assigneeType.TypeName(), c.expectSuccess, got)
		}
	}
}
