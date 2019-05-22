// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package netutil provides network-related helper functions.
package netutil

import (
	"encoding/json"
	"io/ioutil"
	"net/http"
)

// HTTPGet gets a response from the given URL and returns it as a byte slice.
func HTTPGet(url string) ([]byte, error) {
	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return ioutil.ReadAll(resp.Body)
}

// HTTPGetJSON gets a response from the given URL and stores it in jsonData.
// It is the caller's responsibility to make sure the response is in JSON format.
func HTTPGetJSON(url string, jsonData interface{}) error {
	b, err := HTTPGet(url)
	if err != nil {
		return err
	}

	// Responses start with )]}' to prevent XSSI attacks. Discard them.
	// See https://gerrit-review.googlesource.com/Documentation/rest-api.html#output
	return json.Unmarshal(b[4:], jsonData)
}
