// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"fmt"

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
	name Name
	// parent is nonempty for members.
	parent fidlgen.EncodedCompoundIdentifier
}

func newNamed(name fidlgen.EncodedCompoundIdentifier) named {
	return named{name: Name(name)}
}

func (l named) Serialize() elementStr {
	var e elementStr
	e.Name = l.Name()
	return e
}

func (l named) Name() Name {
	if l.parent != "" {
		return Name(fmt.Sprintf("%v.%v", l.parent, l.name))
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
		named:      named{parent: parentName, name: Name(name)},
		parentType: parentType,
	}
}

// String implements Element.
func (i isMember) String() string {
	return i.Serialize().String()
}

func (i isMember) Serialize() elementStr {
	e := i.named.Serialize()
	e.Kind = Kind(fmt.Sprintf("%v/member", i.parentType))
	return e
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
		named:        named{name: Name(name)},
		resourceness: resourceness,
		typeName:     typeName,
	}
}

// String implements Element.
func (s aggregate) String() string {
	return s.Serialize().String()
}

func resourceness(resourceness fidlgen.Resourceness) Resourceness {
	if resourceness {
		return isResource
	}
	return isValue
}

func (s aggregate) Serialize() elementStr {
	e := s.named.Serialize()
	e.Resourceness = resourceness(s.resourceness)
	e.Kind = Kind(s.typeName)
	return e
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
	return s.Serialize().String()
}

// Member implements Element.
func (s member) Member() bool {
	return s.m.Member()
}

// Name implements Element.
func (s member) Name() Name {
	return s.m.Name()
}

func (s member) Serialize() elementStr {
	e := s.m.Serialize()
	e.Decl = fidlTypeString(s.memberType)
	return e
}
