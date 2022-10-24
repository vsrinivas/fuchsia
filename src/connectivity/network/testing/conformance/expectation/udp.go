// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import "go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"

var udpExpectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 1}:  Pass,
	{2, 1}:  Pass,
	{2, 2}:  Pass,
	{2, 3}:  Pass,
	{2, 4}:  Pass,
	{2, 5}:  Pass,
	{2, 6}:  Pass,
	{2, 7}:  Pass,
	{2, 8}:  Pass,
	{2, 9}:  Pass,
	{2, 10}: Pass,
	{2, 11}: Pass,
	{2, 12}: Pass,
	{2, 13}: Pass,
	{3, 1}:  Pass,
	{3, 2}:  Pass,
	{3, 3}:  Pass,
	{3, 4}:  Pass,
	{3, 5}:  Pass,
	{3, 6}:  Pass,
	{3, 7}:  Pass,
	{3, 8}:  Pass,
	{4, 1}:  Pass,
	{5, 1}:  Fail,
	{5, 2}:  Fail,
	{5, 3}:  Pass,
	{6, 3}:  Fail,
	{6, 6}:  Fail,
	{7, 1}:  Pass,
	{7, 3}:  Pass,
	{8, 1}:  Pass,
	{8, 2}:  Pass,
	{9, 1}:  Pass,
	{9, 2}:  Pass,
	{10, 1}: Fail,
}
