// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package apidiff

import (
	"encoding/json"
	"fmt"
)

// Classification is used to classify the API differences according to their
// safety.
type Classification int

const (
	// UnsetClassification is the default field value. It should *never*
	// appear as a valid Classification, and if it does it means that
	// something should have set the field but didn't.
	UnsetClassification Classification = iota
	// Undetermined is some sort of a change, but an additional pass is needed
	// to determine what exactly it should be.  It should not appear in the the
	// serialized output at all, we expect to backfill it once we know its
	// enclosing declaration.
	Undetermined
	// APIBreaking change will break compilation for clients.
	APIBreaking
	// Transitionable change can be made as a sequence of SourceCompatible
	// changes.
	Transitionable
	// SourceCompatible change does not break compilation.
	SourceCompatible
)

var (
	classificationToString = map[Classification]string{
		APIBreaking:      "APIBreaking",
		Transitionable:   "Transitionable",
		SourceCompatible: "SourceCompatible",
	}

	stringToClassification = map[string]Classification{
		"APIBreaking":      APIBreaking,
		"Transitionable":   Transitionable,
		"SourceCompatible": SourceCompatible,
	}
)

func (c Classification) String() string {
	return classificationToString[c]
}

// ToClassification converts the supplied stirng into a Classification.
func ToClassification(s string) Classification {
	return stringToClassification[s]
}

// MarshalYAML implements yaml.Marshaler.
func (c Classification) MarshalYAML() (interface{}, error) {
	return c.String(), nil
}

// UnmarshalYAML implements yaml.Unmarshaler.
func (c *Classification) UnmarshalYAML(u func(interface{}) error) error {
	var s string
	if err := u(&s); err != nil {
		return fmt.Errorf("while unmarshaling: %v: %w", c, err)
	}
	*c = ToClassification(s)
	return nil
}

// MarshalJSON implements json.Marshaler.
func (c Classification) MarshalJSON() ([]byte, error) {
	return json.Marshal(c.String())
}

// UnmarshalJSON implements json.Unmarshaler.
func (c *Classification) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return fmt.Errorf("while unmarshaling Classification: %w", err)
	}
	*c = ToClassification(s)
	return nil
}
