// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import "go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"

var icmpExpectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 1}:  Pass,
	{1, 5}:  Pass,
	{2, 1}:  Pass,
	{2, 2}:  Pass,
	{2, 3}:  Pass,
	{2, 4}:  Pass,
	{2, 5}:  Pass,
	{3, 1}:  Pass,
	{4, 2}:  Pass,
	{4, 3}:  Fail,
	{4, 4}:  Pass,
	{5, 1}:  Pass,
	{5, 2}:  Pass,
	{5, 3}:  Pass,
	{8, 1}:  Pass,
	{8, 2}:  Pass,
	{8, 3}:  Pass,
	{10, 1}: Pass,
}
