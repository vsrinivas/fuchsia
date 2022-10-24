// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package expectation

import (
	"go.fuchsia.dev/fuchsia/src/connectivity/network/testing/conformance/expectation/outcome"
)

var arpExpectations map[AnvlCaseNumber]outcome.Outcome = map[AnvlCaseNumber]outcome.Outcome{
	{1, 1}:  Pass,
	{2, 1}:  Pass,
	{2, 2}:  Pass,
	{2, 3}:  Fail,
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
	{2, 14}: Pass,
	{2, 15}: Pass,
	{2, 16}: Pass,
	{3, 1}:  Pass,
	{3, 2}:  Pass,
	{3, 3}:  Fail,
	{3, 4}:  Pass,
	{3, 5}:  Pass,
	{3, 6}:  Pass,
	{3, 7}:  Fail,
	{3, 8}:  Pass,
	{3, 9}:  Pass,
	{3, 10}: Pass,
	{3, 11}: Fail,
	{3, 12}: Pass,
	{3, 13}: Pass,
	{3, 14}: Pass,
	{3, 15}: Fail,
	{3, 16}: Pass,
	{3, 17}: Pass,
	{3, 18}: Fail,
	{3, 19}: Fail,
	{3, 20}: Pass,
	{3, 21}: Pass,
	{3, 22}: Pass,
	{3, 23}: Fail,
	{3, 24}: Pass,
	{3, 25}: Pass,
	{3, 26}: Pass,
	{3, 27}: Pass,
	{3, 28}: Pass,
	{3, 29}: Pass,
	{3, 30}: Pass,
	{3, 31}: Pass,
	{4, 1}:  Pass,
	{4, 2}:  Pass,
	{6, 1}:  Pass,
	{6, 2}:  Inconclusive,
}
