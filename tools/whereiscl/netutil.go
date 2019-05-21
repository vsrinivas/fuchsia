// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"io/ioutil"
	"net/http"
)

// httpGet gets a response from the given URL and returns it as a byte slice.
func httpGet(url string) ([]byte, error) {
	resp, err := http.Get(url)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return ioutil.ReadAll(resp.Body)
}

// httpGetJSON gets a response from the given URL and stores it in jsonData.
// It is the caller's responsibility to make sure the response is in JSON format.
func httpGetJSON(url string, jsonData interface{}) error {
	b, err := httpGet(url)
	if err != nil {
		return err
	}

	// Responses start with )]}' to prevent XSSI attacks. Discard them.
	// See https://gerrit-review.googlesource.com/Documentation/rest-api.html#output
	return json.Unmarshal(b[4:], jsonData)
}
