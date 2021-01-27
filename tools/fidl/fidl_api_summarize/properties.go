// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"fmt"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

/*
Implementations of properties shared between multple Element types.  If an
Element type needs a property, a property type is embedded into its type.  For
an example, see below how isMember embeds named.
*/

// named is something that has a name.
type named struct {
	// name is a fully qualified name. It is generic, as sometimes it is a
	// compound identifier and sometimes "just" an identifier.
	name string
	// parent is nonempty for members.
	parent fidlgen.EncodedCompoundIdentifier
}

func newNamed(name fidlgen.EncodedCompoundIdentifier) named {
	return named{name: string(name)}
}

func (l named) Name() string {
	if l.parent != "" {
		return fmt.Sprintf("%v.%v", l.parent, l.name)
	}
	return l.name
}

// isMember is something that is a member.
type isMember struct {
	named
	parentType fidlgen.DeclType
}

// newIsMember creates a new element that represents a member.
func newIsMember(
	parentName fidlgen.EncodedCompoundIdentifier,
	name fidlgen.Identifier,
	parentType fidlgen.DeclType) isMember {
	return isMember{
		named:      named{parent: parentName, name: string(name)},
		parentType: parentType,
	}
}

// String implements Element.
func (i isMember) String() string {
	return fmt.Sprintf("%v/member %v", i.parentType, i.Name())
}

func (m isMember) Member() bool {
	return true
}

// notMember is something that is not a member.
type notMember struct{}

func (m notMember) Member() bool {
	return false
}

// aggregate is the base Element for anything that looks like a struct, API-wise.
type aggregate struct {
	named
	notMember
	resourceness fidlgen.Resourceness
	typeName     fidlgen.DeclType
}

func newAggregate(
	name fidlgen.EncodedCompoundIdentifier,
	resourceness fidlgen.Resourceness,
	typeName fidlgen.DeclType) aggregate {
	return aggregate{
		named:        named{name: string(name)},
		resourceness: resourceness,
		typeName:     typeName,
	}
}

// String implements Element.
func (s aggregate) String() string {
	var ret []string
	if s.resourceness {
		ret = append(ret, "resource")
	}
	ret = append(ret, string(s.typeName), s.name)
	return strings.Join(ret, " ")
}

// member is an element of an aggregate (e.g. field of a struct).
type member struct {
	m          isMember
	memberType fidlgen.Type
}

// newMember creates a new aggregate member element.
func newMember(
	parentName fidlgen.EncodedCompoundIdentifier,
	name fidlgen.Identifier,
	memberType fidlgen.Type,
	declType fidlgen.DeclType) member {
	return member{
		m:          newIsMember(parentName, name, declType),
		memberType: memberType,
	}
}

// String implements Element.
func (s member) String() string {
	return fmt.Sprintf("%v %v", s.m.String(), fidlTypeString(s.memberType))
}

// Member implements Element.
func (s member) Member() bool {
	return s.m.Member()
}

// Name implements Element.
func (s member) Name() string {
	return s.m.Name()
}
