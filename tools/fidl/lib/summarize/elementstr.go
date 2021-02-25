// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package summarize

import "strings"

// Strictness is whether an aggregate is strict or flexible.
type Strictness string

var (
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

// Kind is the encoding of the type, e.g. const
type Kind string

// Decl is the encoding of the type declaration.
type Decl string

// Name is the fully qualified name of the element.
type Name string

// elementStr is a generic stringly-typed view of an Element. The aim is to
// keep the structure as flat as possible, and omit fields which have no
// bearing to the Kind of element represented.
type elementStr struct {
	Name         `json:"name"`
	Kind         `json:"kind"`
	Decl         `json:"declaration,omitempty"`
	Strictness   `json:"strictness,omitempty"`
	Resourceness `json:"resourceness,omitempty"`
}

func (e elementStr) String() string {
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
	return strings.Join(p, " ")
}
