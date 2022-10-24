// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import "go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"

var tcpHighperfExpectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 17}: Pass,
	{1, 18}: Pass,
	{1, 19}: Pass,
	{1, 20}: Pass,
	{1, 21}: Pass,
	{1, 22}: Pass,
	{2, 17}: Pass,
	{2, 18}: Pass,
	{2, 19}: Pass,
	{2, 20}: Pass,
	{2, 21}: Pass,
	{2, 22}: Pass,
	{2, 23}: Pass,
	{2, 24}: Fail,
	{3, 17}: Pass,
	{3, 18}: Pass,
	{3, 19}: Pass,
	{3, 20}: Pass,
	{3, 21}: Pass,
	{3, 22}: Pass,
	{3, 23}: Pass,
	{3, 24}: Pass,
	{3, 25}: Pass,
	{3, 26}: Pass,
	{3, 27}: Pass,
	{3, 28}: Pass,
	{3, 29}: Pass,
	{3, 30}: Flaky, // TODO(https://fxbug.dev/105201): Fix flake.
	{3, 31}: Pass,
	{3, 32}: Pass,
	{4, 17}: Fail,
	{4, 18}: Fail,
	{4, 19}: Pass,
	{4, 20}: Flaky, // TODO(https://fxbug.dev/105258): Fix flake.
	{5, 18}: Pass,
	{5, 19}: Pass,
	{5, 20}: Fail,
	{5, 21}: Pass,
	{6, 17}: Pass,
	{6, 18}: Pass,
	{6, 19}: Pass,
	{7, 17}: Pass,
	{7, 18}: Pass,
	{7, 19}: Pass,
	{7, 20}: Pass,
	{7, 21}: Pass,
	{7, 22}: Pass,
	{7, 23}: Pass,
	{7, 24}: Fail,
}
