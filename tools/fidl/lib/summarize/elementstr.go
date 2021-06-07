// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import (
	"encoding/json"
	"fmt"
	"io"
	"strings"

	"go.fuchsia.dev/fuchsia/tools/fidl/lib/fidlgen"
)

// Strictness is whether an aggregate is strict or flexible.
type Strictness string

var (
	// noStrict denotes no defined strictness for this element.
	noStrict Strictness = ""
	// isStrict strictness value.
	isStrict Strictness = "strict"
	// isFlexible strictness value.
	isFlexible Strictness = "flexible"
)

// Resourceness is whether the aggregate has resources or not.
type Resourceness string

var (
	// isValue resourceness is the "default", so we omit it altogether.
	isValue Resourceness = ""
	// isResource is an aggregate that contains e.g. handles.
	isResource Resourceness = "resource"
)

// Kind is the encoding of the type, e.g. 'const'.
type Kind string

// Decl is the underlying type declaration.  For `enum Foo : int32`,
// this will be `int32`.
type Decl string

// Name is the fully qualified name of the element.
type Name string

// Value is a string-serialized value of the element.
// Since for the time being the typed value is not necessary, this is quite
// enough to pipe the element value through where needed.
type Value string

// fidlConstToValue converts the fidlgen view of a constant value to
// summary's Value.
func fidlConstToValue(fc *fidlgen.Constant) Value {
	if fc == nil {
		return Value("")
	}
	// It looks like any value type has its value in fc.Value.
	return Value(fc.Value)
}

// ElementStr is a generic stringly-typed view of an Element. The aim is to
// keep the structure as flat as possible, and omit fields which have no
// bearing to the Kind of element represented.
//
// Keep the element ordering sorted.
type ElementStr struct {
	Decl         `json:"declaration,omitempty"`
	Kind         `json:"kind"`
	Name         `json:"name"`
	Resourceness `json:"resourceness,omitempty"`
	Strictness   `json:"strictness,omitempty"`
	Value        `json:"value,omitempty"`
}

func (e ElementStr) String() string {
	var p []string
	if e.Resourceness != "" {
		p = append(p, string(e.Resourceness))
	}
	if e.Strictness != "" {
		p = append(p, string(e.Strictness))
	}
	p = append(p, string(e.Kind), string(e.Name))
	if e.Decl != "" {
		p = append(p, string(e.Decl))
	}
	if e.Value != "" {
		if e.Decl == "string" {
			// Quote strings to disambiguate between "foo" and " foo", for
			// example.
			p = append(p, fmt.Sprintf("%q", e.Value))
		} else {
			p = append(p, string(e.Value))
		}
	}
	return strings.Join(p, " ")
}

func (e ElementStr) Less(other ElementStr) bool {
	n1 := newFqn(Name(e.Name))
	n2 := newFqn(Name(other.Name))
	return n1.Less(n2)
}

// IsStrict returns true of this element is strict. The result makes sense only
// on elements that have a defined strictness.
func (e ElementStr) IsStrict() bool {
	return e.Strictness == isStrict
}

// HasStrictness returns true if this ElementStr has a notion of strictness, as
// not all ElementStrs do.
func (e ElementStr) HasStrictness() bool {
	return e.Strictness != noStrict
}

// LoadSummariesJSON loads several the API summaries in the JSON format from
// the given reader readers. Returns the respective summaries in the order of
// supplied readers, or the first encountered error.
func LoadSummariesJSON(rs ...io.Reader) ([][]ElementStr, error) {
	var rets [][]ElementStr
	for _, r := range rs {
		var ret []ElementStr
		d := json.NewDecoder(r)
		if err := d.Decode(&ret); err != nil {
			return nil, fmt.Errorf("while decoding the summary: %w", err)
		}
		rets = append(rets, ret)
	}
	return rets, nil
}
