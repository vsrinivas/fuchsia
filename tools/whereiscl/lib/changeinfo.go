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
	"os/exec"
	"regexp"
	"strings"

	"fuchsia.googlesource.com/fuchsia/tools/whereiscl/netutil"
)

const fuchsiaReviewURL = "https://fuchsia-review.googlesource.com"

// Regexp's for matching CL review URLs and extracting the CL numbers or Change-Id.
// Supports various forms. See the usage examples in the whereiscl.go file.
var (
	fuchsiaCLNumRE    = regexp.MustCompile(`^(?:https?://)?(?:fxr|fuchsia-review.googlesource.com/c/.+/\+)/(\d+).*$`)
	fuchsiaChangeIdRE = regexp.MustCompile(`^(?:https?://)?(?:fxr|fuchsia-review.googlesource.com/c/.+/\+)/([^/]+)$`)
	rawCLOrChangeIdRE = regexp.MustCompile(`^([^/]+)$`)
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
	Number          int      `json:"_number"`
}

// QueryInfo stores information for querying the Gerrit server.
type QueryInfo struct{ APIEndpoint, Query string }

// ParseReviewURL parses the given string and returns QueryInfo.
func ParseReviewURL(str string) (*QueryInfo, error) {
	for _, re := range []*regexp.Regexp{fuchsiaCLNumRE, fuchsiaChangeIdRE, rawCLOrChangeIdRE} {
		match := re.FindStringSubmatch(str)
		if match != nil {
			return &QueryInfo{
				APIEndpoint: fuchsiaReviewURL,
				Query:       match[1],
			}, nil
		}
	}

	return nil, errors.New("not a valid review URL")
}

// ParseGitRevision parses the given git revision and returns QueryInfo.
// `str` is any string that can be parsed by `git rev-parse`.
func ParseGitRevision(str string) (*QueryInfo, error) {
	cmd := exec.Command("git", "rev-parse", str)
	cmd.Stderr = nil
	out, err := cmd.Output()
	if err != nil {
		switch v := err.(type) {
		case *exec.ExitError:
			return nil, fmt.Errorf("%v: %s", err, v.Stderr)
		default:
			return nil, err
		}
	}
	return &QueryInfo{
		APIEndpoint: fuchsiaReviewURL,
		Query:       strings.TrimSpace(string(out)),
	}, nil
}

func makeQueryURL(qi *QueryInfo) (*url.URL, error) {
	u, err := url.Parse(qi.APIEndpoint)
	if err != nil {
		return nil, err
	}
	u.Path = "/changes/"
	q := u.Query()
	q.Set("q", qi.Query)
	q.Add("o", "CURRENT_REVISION")
	u.RawQuery = q.Encode()
	return u, nil
}

// GetChangeInfo retrieves a ChangeInfo from Gerrit about a given query.
func GetChangeInfo(qi *QueryInfo) (*ChangeInfo, error) {
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
