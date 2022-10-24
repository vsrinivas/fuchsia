// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import (
	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"
)

var igmpExpectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 1}:  Pass,
	{2, 9}:  Pass,
	{2, 10}: Pass,
	{2, 11}: Pass,
	{3, 1}:  Pass,
	{3, 4}:  Pass,
	{3, 5}:  Pass,
	{3, 6}:  Flaky, // TODO(https://fxbug.dev/104720): Investigate flake.
	{3, 7}:  Pass,
	{5, 1}:  Pass,
	{5, 2}:  Pass,
	{5, 3}:  Pass,
	{5, 5}:  Pass,
	{5, 7}:  Pass,
	{5, 8}:  Fail,
	{5, 9}:  Fail,
	{5, 11}: Fail,
	{6, 2}:  Pass,
	{6, 8}:  Pass,
}
