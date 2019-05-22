// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Package lib provides features that are needed for the whereiscl tool.
// The changeinfo.go file includes functions for getting the CL status on Gerrit.
// The giStatus.go file includes functions for telling whether a CL passed
// Global Integration.
package lib

import (
	"errors"
	"fmt"
	"net/url"

	"fuchsia.googlesource.com/fuchsia/tools/whereiscl/netutil"
)

// CLStatus represents the status of a CL in Gerrit.
type CLStatus string

const (
	CLStatusNew       CLStatus = "NEW"
	CLStatusMerged    CLStatus = "MERGED"
	CLStatusAbandoned CLStatus = "ABANDONED"
)

// ChangeInfo is a JSON struct for ChangeInfo responses from Gerrit.
// https://gerrit-review.googlesource.com/Documentation/rest-api-changes.html#change-info.
// Only fields of interest are listed here.
type ChangeInfo struct {
	Project         string   `json:"project"`
	Status          CLStatus `json:"status"`
	CurrentRevision string   `json:"current_revision"`
}

// QueryInfo stores information for querying the Gerrit server.
type QueryInfo struct{ APIEndpoint, CL string }

func makeQueryURL(qi QueryInfo) (*url.URL, error) {
	u, err := url.Parse(qi.APIEndpoint)
	if err != nil {
		return nil, err
	}
	u.Path = "/changes/"
	q := u.Query()
	q.Set("q", qi.CL)
	q.Add("o", "CURRENT_REVISION")
	u.RawQuery = q.Encode()
	return u, nil
}

// GetChangeInfo retrieves a ChangeInfo from Gerrit about a given CL.
func GetChangeInfo(qi QueryInfo) (*ChangeInfo, error) {
	q, err := makeQueryURL(qi)
	if err != nil {
		return nil, err
	}
	var cis []ChangeInfo
	if err := netutil.HTTPGetJSON(q.String(), &cis); err != nil {
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
