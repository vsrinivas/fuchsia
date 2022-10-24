// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import "go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"

var ipExpectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 1}:  Pass,
	{1, 3}:  Pass,
	{2, 3}:  Pass,
	{2, 4}:  Pass,
	{3, 4}:  Pass,
	{3, 5}:  Pass,
	{3, 6}:  Pass,
	{3, 8}:  Pass,
	{4, 1}:  Pass,
	{4, 2}:  Pass,
	{4, 4}:  Pass,
	{4, 5}:  Pass,
	{5, 1}:  Pass,
	{5, 6}:  Pass,
	{6, 1}:  Pass,
	{6, 2}:  Pass,
	{6, 3}:  Pass,
	{6, 4}:  Pass,
	{6, 5}:  Pass,
	{6, 7}:  Pass,
	{6, 8}:  Pass,
	{6, 9}:  Pass,
	{6, 10}: Pass,
	{6, 12}: Pass,
	{7, 1}:  Pass,
	{7, 3}:  Fail,
	{7, 4}:  Pass,
	{7, 5}:  Pass,
	{7, 6}:  Inconclusive,
}
