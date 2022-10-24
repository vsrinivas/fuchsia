// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import "go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"

var tcpAdvancedV6Expectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 17}: Fail,
	{1, 18}: Fail,
	{2, 18}: Fail,
	{2, 19}: Fail,
	{2, 20}: Fail,
	{2, 21}: Fail,
	{2, 22}: Fail,
	{2, 23}: Fail,
	{2, 24}: Fail,
	{2, 25}: Fail,
	{3, 17}: Flaky, // TODO(https://fxbug.dev/105174): Fix the flake.
	{4, 17}: Pass,
	{6, 17}: Flaky, // TODO(https://fxbug.dev/105257): Fix the flake.
}
