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

var toName = cmp.Transformer("ToName", func(m Method) string {
	return m.Name
})

func TestMatchOneWayMethods(t *testing.T) {
	cmp.Diff(mockProtocol.OneWayMethods, []string{"OneWay"}, toName)
}

func TestMatchTwoWayMethods(t *testing.T) {
	cmp.Diff(mockProtocol.TwoWayMethods, []string{"TwoWay"}, toName)
}

func TestMatchClientMethods(t *testing.T) {
	cmp.Diff(mockProtocol.ClientMethods, []string{"OneWay", "TwoWay"}, toName)
}

func TestMatchEvents(t *testing.T) {
	cmp.Diff(mockProtocol.Events, []string{"Event"}, toName)
}
