// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

// This file contains the data model for the clang_doc files.
//
// The data model was reverse engineered from sample clang_doc output, so may
// need to be adjusted; and/or evolved over time as clang_doc evolves.
//
// In the declared structs the fields are sorted alphanumerically by name. This
// does not give a pretty go formatting, but is likely the only way to keep
// track of the coverage of the recovered data model.

package main

import (
	"fmt"
	"io"

	"gopkg.in/yaml.v2"
)

// Needs serdes. Unclear why Type is not the same as TagType.
type Type string

const (
	TypeClass     Type = "Class"
	TypeNamespace Type = "Namespace"
	TypeRecord    Type = "Record"
)

var typeFromString = map[string]Type{
	"Class":     TypeClass,
	"Namespace": TypeNamespace,
	"Record":    TypeRecord,
}

var _ yaml.Unmarshaler = (*Type)(nil)

func (t *Type) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var s string
	if err := unmarshal(&s); err != nil {
		return fmt.Errorf("could not unmarshal Type: %w", err)
	}
	u := typeFromString[s]
	t = &u
	return nil
}

// USR is a unique symbol resolution identifier.
type USR string

// Name is an unqualified name for a symbol.
type Name string

// ID contains the complete information about a symbol and its type.
type ID struct {
	// Name is an unqualified name of an identifier
	Name `yaml:"Name,omitempty"`
	Path `yaml:"Path,omitempty"`
	// In contrast to other fields, this one can not be an empbedded type, as
	// the custom unmarshaller will attempt to submit a YAML representation of
	// ID to the parser for Type.  Not sure why it thinks that would be OK.
	Type Type `yaml:"Type,omitempty"`
	// USR seems to be a unique identifier for each symbol.
	USR `yaml:"USR,omitempty"`
}

// NamespaceID is a fully qualified namespace identifier
type NamespaceID struct {
	ID                  ID   `yaml:",inline"`
	IsInGlobalNamespace bool `yaml:"IsInGlobalNamespace,omitempty"`
}

type Path string

type DefLocation struct {
	Filename   string `yaml:"Filename"`
	LineNumber int    `yaml:"LineNumber"`
}

type ParamType struct {
	Name `yaml:"Name"`
	Type ID `yaml:"Type"`
}

type Access string

const (
	AccessPublic    Access = "Public"
	AccessPrivate   Access = "Private"
	AccessProtected Access = "Protected"
)

var accessFromString = map[string]Access{
	"Public":    AccessPublic,
	"Private":   AccessPrivate,
	"Protected": AccessProtected,
}

var _ yaml.Unmarshaler = (*Access)(nil)

func (a *Access) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var s string
	if err := unmarshal(&s); err != nil {
		return fmt.Errorf("could not unmarshal Access: %w", err)
	}
	u := accessFromString[s]
	a = &u
	return nil
}

// ChildFunction describes a function (possibly a method, too), enclosed within
// a parent (namespace in case of free functions; class in case of methods).
type ChildFunction struct {
	Access      Access `yaml:"Access"`
	DefLocation `yaml:"DefLocation"`
	Description GenericJSON   `yaml:"Description,omitempty"`
	IsMethod    bool          `yaml:"IsMethod"`
	Location    []DefLocation `yaml:"Location,omitempty"`
	Name        `yaml:"Name"`
	Namespace   []ID        `yaml:"Namespace"`
	Params      []ParamType `yaml:"Params"`
	Parent      ID          `yaml:"Parent"`
	ReturnType  ParamType   `yaml:"ReturnType,omitempty"`
	USR         `yaml:"USR"`
}

// fullName returns the fully qualified name of a function.
func (c ChildFunction) fullName() string {
	return fullName(c.Name, c.Namespace)
}

// GenericJSON is a catch-all for parts of the report we do not
// care about currently.
type GenericJSON interface{}

// ChildEnum defines an enumeration (usually defined as a child of a namespace
// it is nested in).
type ChildEnum struct {
	DefLocation `yaml:"DefLocation"`
	Description GenericJSON `yaml:"Description"`
	Members     []string    `yaml:"Members,omitempty"`
	Name        `yaml:"Name"`
	Namespace   []ID `yaml:"Namespace"`
	Scoped      bool `yaml:"Scoped,omitempty"`
	USR         `yaml:"USR"`
}

// Member defines a class member.
type Member struct {
	// Access denotes one of private, protected or public access.
	Access Access `yaml:"Access,omitempty"`
	Name   `yaml:"Name,omitempty"`
	Type   ID `yaml:"Type"`
}

// Aggregate is the top-level entity in each of the YAML files that clang-doc
// produces.
type Aggregate struct {
	// Access shows whether the aggregate is public or not.
	Access Access `yaml:"Access"`
	/// Bases contain references to base classes for this aggregate, if applicable.
	Bases           []Aggregate     `yaml:"Bases"`
	ChildEnums      []ChildEnum     `yaml:"ChildEnums,omitempty"`
	ChildFunctions  []ChildFunction `yaml:"ChildFunctions,omitempty"`
	ChildNamespaces []NamespaceID   `yaml:"ChildNamespaces,omitempty"`
	ChildRecords    []ID            `yaml:"ChildRecords,omitempty"`
	DefLocation     `yaml:"DefLocation"`
	Description     GenericJSON   `yaml:"Description,omitempty"`
	IsParent        bool          `yaml:"IsParent"`
	Location        []DefLocation `yaml:"Location,omitempty"`
	Members         []Member      `yaml:"Members,omitempty"`
	Name            `yaml:"Name"`
	Namespace       []ID `yaml:"Namespace"`
	Parents         []ID `yaml:"Parents,omitempty"`
	Path            `yaml:"Path"`
	TagType         Type `yaml:"TagType,omitempty"`
	USR             `yaml:"USR"`
}

// ParseYAML reads an Aggregate from the supplied reader.
func ParseYAML(r io.Reader) (Aggregate, error) {
	d := yaml.NewDecoder(r)
	// Without this, we'd have no idea whether YAML parsing made sense.
	d.SetStrict(true)
	var ret Aggregate
	if err := d.Decode(&ret); err != nil {
		return ret, fmt.Errorf("while reading YAML: %w", err)
	}
	return ret, nil
}
