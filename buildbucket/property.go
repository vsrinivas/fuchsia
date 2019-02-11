// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package buildbucket

import (
	structpb "github.com/golang/protobuf/ptypes/struct"
)

// Property is a Build input property.
//
// Implement different "TypedValue" methods on this type as needed.
type Property struct {
	name  string
	value *structpb.Value
}

// Name returns the name of this property.
func (p Property) Name() string {
	return p.name
}

// StringValue returns the value of this property as a string. Returns the empty string if
// the value is unset or is not a string.
func (p Property) StringValue() string {
	return p.value.GetStringValue()
}
