// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import "go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"

var icmpv6Expectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 1}:  Pass,
	{1, 2}:  Pass,
	{2, 1}:  Fail,
	{2, 3}:  Pass,
	{3, 1}:  Inconclusive,
	{3, 2}:  Pass,
	{4, 1}:  Pass,
	{4, 2}:  Pass,
	{4, 3}:  Pass,
	{4, 4}:  Pass,
	{4, 6}:  Pass,
	{4, 7}:  Pass,
	{4, 9}:  Pass,
	{4, 10}: Pass,
	{4, 12}: Fail,
	{4, 13}: Pass,
	{5, 7}:  Pass,
	{5, 11}: Pass,
	{5, 12}: Pass,
	{8, 1}:  Pass,
	{8, 2}:  Pass,
	{8, 3}:  Pass,
	{8, 4}:  Pass,
	{8, 5}:  Pass,
	{9, 1}:  Pass,
	{9, 2}:  Pass,
	{10, 1}: Pass,
}
