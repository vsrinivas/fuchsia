// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fidlgen_cpp

import (
	"testing"

	"github.com/google/go-cmp/cmp"
)

var mockProtocol = protocolInner{
	Methods: []Method{
		{
			Name:        "OneWay",
			HasRequest:  true,
			HasResponse: false,
		},
		{
			Name:        "TwoWay",
			HasRequest:  true,
			HasResponse: true,
		},
		{
			Name:        "Event",
			HasRequest:  false,
			HasResponse: true,
		},
	},
}.build()

func toNames(methods []Method) []string {
	var s []string
	for _, m := range methods {
		s = append(s, m.Name)
	}
	return s
}

func assertEmpty(t *testing.T, diff string) {
	t.Helper()
	if len(diff) > 0 {
		t.Fatalf("Expected empty diff, got %s", diff)
	}
}

func TestMatchOneWayMethods(t *testing.T) {
	assertEmpty(t, cmp.Diff(toNames(mockProtocol.OneWayMethods), []string{"OneWay"}))
}

func TestMatchTwoWayMethods(t *testing.T) {
	assertEmpty(t, cmp.Diff(toNames(mockProtocol.TwoWayMethods), []string{"TwoWay"}))
}

func TestMatchClientMethods(t *testing.T) {
	assertEmpty(t, cmp.Diff(toNames(mockProtocol.ClientMethods), []string{"OneWay", "TwoWay"}))
}

func TestMatchEvents(t *testing.T) {
	assertEmpty(t, cmp.Diff(toNames(mockProtocol.Events), []string{"Event"}))
}
