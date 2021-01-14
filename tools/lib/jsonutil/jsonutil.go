// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package jsonutil

import (
	"encoding/json"
	"io/ioutil"
)

// WriteToFile writes data as JSON into a file.
func WriteToFile(path string, v interface{}) error {
	raw, err := json.Marshal(v)
	if err != nil {
		return err
	}
	return ioutil.WriteFile(path, raw, 0o600)
}

// ReadFromFile reads data as JSON from a file.
func ReadFromFile(path string, v interface{}) error {
	raw, err := ioutil.ReadFile(path)
	if err != nil {
		return err
	}
	return json.Unmarshal(raw, v)
}
