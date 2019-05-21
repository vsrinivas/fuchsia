// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"errors"
	"fmt"
	"log"
	"os"
	"regexp"
)

const fuchsiaReviewURL = "https://fuchsia-review.googlesource.com"

// fuchsiaRE is a regexp for matching CL review URLs and extracting the CL numbers.
// Supports various forms. E.g.,
//   - https://fuchsia-review.googlesource.com/c/fuchsia/+/123456789
//   - fuchsia-review.googlesource.com/c/fuchsia/+/123456789/some/file
//   - http://fxr/123456789
//   - fxr/123456789/some/file
var fuchsiaRE = regexp.MustCompile(`^(?:https?://)?(?:fxr|fuchsia-review.googlesource.com/c/.+/\+)/(\d+).*`)

func parseReviewURL(str string) (queryInfo, error) {
	match := fuchsiaRE.FindStringSubmatch(str)
	if match != nil {
		return queryInfo{
			apiEndpoint: fuchsiaReviewURL,
			cl:          match[1],
		}, nil
	}

	return queryInfo{}, errors.New("not a valid review URL")
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

	ci, err := getChangeInfo(queryInfo)
	if err != nil {
		log.Fatalf("Error getting change info: %v", err)
	}
	fmt.Printf("CL status: %v\n", ci.Status)

	if ci.Status != clStatusMerged {
		return
	}

	gs, err := getGIStatus(ci)
	if err != nil {
		log.Fatalf("Error getting GI status: %v", err)
	}
	fmt.Printf("GI status: %v\n", gs)
}
