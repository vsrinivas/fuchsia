// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import "go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"

var ipv6PmtuExpectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{2, 1}: Pass,
	{2, 2}: Fail,
	{3, 1}: Fail,
	{3, 2}: Fail,
	{3, 3}: Fail,
	{3, 4}: Fail,
	{4, 2}: Fail,
	{5, 1}: Fail,
}
