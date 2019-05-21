// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"net/http"
	"net/url"
)

type clStatus string

const (
	clStatusNew       clStatus = "NEW"
	clStatusMerged    clStatus = "MERGED"
	clStatusAbandoned clStatus = "ABANDONED"
)

// changeInfo is a JSON struct for ChangeInfo responses from Gerrit.
// https://gerrit-review.googlesource.com/Documentation/rest-api-changes.html#change-info.
// Only fields of interest are listed here.
type changeInfo struct {
	Project         string   `json:"project"`
	Status          clStatus `json:"status"`
	CurrentRevision string   `json:"current_revision"`
}

type queryInfo struct{ apiEndpoint, cl string }

func makeQueryURL(qi queryInfo) (*url.URL, error) {
	u, err := url.Parse(qi.apiEndpoint)
	if err != nil {
		return nil, err
	}
	u.Path = "/changes/"
	q := u.Query()
	q.Set("q", qi.cl)
	q.Add("o", "CURRENT_REVISION")
	u.RawQuery = q.Encode()
	return u, nil
}

func getChangeInfo(qi queryInfo) (*changeInfo, error) {
	q, err := makeQueryURL(qi)
	if err != nil {
		return nil, err
	}
	resp, err := http.Get(q.String())
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	var cis []changeInfo
	// Responses start with )]}' to prevent XSSI attacks. Discard them.
	// See https://gerrit-review.googlesource.com/Documentation/rest-api.html#output
	if err := json.Unmarshal(b[4:], &cis); err != nil {
		return nil, err
	}

	switch len(cis) {
	case 0:
		return nil, errors.New("CL not found")
	case 1:
		return &cis[0], nil
	default:
		return nil, fmt.Errorf("Got %d CLs while expecting only one", len(cis))
	}
}
