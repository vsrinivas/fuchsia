// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"regexp"
)

const fuchsiaURL = "https://fuchsia-review.googlesource.com"

// fuchsiaRE is a regexp for matching CL review URLs and extracting the CL numbers.
// Supports various forms. E.g.,
//   - https://fuchsia-review.googlesource.com/c/fuchsia/+/123456789
//   - fuchsia-review.googlesource.com/c/fuchsia/+/123456789/some/file
//   - http://fxr/123456789
//   - fxr/123456789/some/file
var fuchsiaRE = regexp.MustCompile(`^(?:https?://)?(?:fxr|fuchsia-review.googlesource.com/c/.+/\+)/(\d+).*`)

type queryInfo struct{ apiEndpoint, cl string }

func parseReviewURL(str string) (queryInfo, error) {
	match := fuchsiaRE.FindStringSubmatch(str)
	if match != nil {
		return queryInfo{
			apiEndpoint: fuchsiaURL,
			cl:          match[1],
		}, nil
	}

	return queryInfo{}, errors.New("not a valid review URL")
}

// changeInfo is a JSON struct for ChangeInfo responses from Gerrit.
// https://gerrit-review.googlesource.com/Documentation/rest-api-changes.html#change-info.
// Only fields of interest are listed here.
type changeInfo struct {
	Status string `json:"status"`
}

func getCLStatus(qi queryInfo) (string, error) {
	query := fmt.Sprintf("%s/changes/?q=%s", qi.apiEndpoint, qi.cl)
	resp, err := http.Get(query)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	b, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}

	var cis []changeInfo
	// Responses start with )]}' to prevent XSSI attacks. Discard them.
	// See https://gerrit-review.googlesource.com/Documentation/rest-api.html#output
	if err := json.Unmarshal(b[4:], &cis); err != nil {
		return "", err
	}

	switch len(cis) {
	case 0:
		return "", errors.New("CL not found")
	case 1:
		return cis[0].Status, nil
	default:
		return "", fmt.Errorf("Got %d CLs while expecting only one", len(cis))
	}
}

func main() {
	if len(os.Args) < 2 {
		// TODO: Consider alternatives. E.g., show all outstanding CLs
		// of the current user, or show all CLs that are pending in
		// Global Integration.
		log.Fatal("Review URL must be provided")
	}

	queryInfo, err := parseReviewURL(os.Args[1])
	if err != nil {
		log.Fatalf("Error parsing the review URL: %v", err)
	}

	status, err := getCLStatus(queryInfo)
	if err != nil {
		log.Fatalf("Error getting change info: %v", err)
	}
	fmt.Printf("CL status: %v\n", status)
}
