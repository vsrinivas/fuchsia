// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"reflect"
)

// TODO(fxbug.dev/47320): Implement shorter, mutable tokens by using a path to a tmpdir

// A Handle provides a way to serialize information about an Instance.
// A Handle must be immutable for the lifetime of the Instance.
type Handle []byte

// Serialize returns a printable representation of the Handle
func (h Handle) Serialize() string {
	return base64.StdEncoding.EncodeToString(h)
}

// PopulateObject will load configuration data from the Handle into the provided object
// Field names must be globally unique
func (h Handle) PopulateObject(obj interface{}) error {
	if err := json.Unmarshal(h, obj); err != nil {
		return fmt.Errorf("error unmarshaling: %s", err)
	}

	return nil
}

// LoadHandleFromString creates a Handle from its string representation as returned by Serialize()
func LoadHandleFromString(s string) (Handle, error) {
	data, err := base64.StdEncoding.DecodeString(s)
	if err != nil {
		return nil, fmt.Errorf("Error decoding handle: %s", err)
	}
	return data, nil
}

// NewHandleFromObjects creates a Handle encoding data from the provided objects
// All exported fields of the objects will be captured, and merged
func NewHandleFromObjects(objs ...interface{}) (Handle, error) {
	mergedData := make(map[string]interface{})

	// Merge the data from the sub-objects
	for _, obj := range objs {
		if obj == nil {
			continue
		}

		t := reflect.TypeOf(obj).Elem()
		v := reflect.ValueOf(obj).Elem()
		for i := 0; i < t.NumField(); i++ {
			field := t.Field(i)
			// Only process exported fields (https://golang.org/pkg/reflect/#StructField)
			if field.PkgPath == "" {
				mergedData[field.Name] = v.Field(i).Interface()
			}
		}
	}

	data, err := json.Marshal(mergedData)
	if err != nil {
		return nil, fmt.Errorf("error constructing instance handle: %s", err)
	}

	return data, nil
}
