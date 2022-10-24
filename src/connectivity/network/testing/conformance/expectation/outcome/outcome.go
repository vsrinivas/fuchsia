// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package outcome

import (
	"fmt"
)

type Outcome int

const (
	_ Outcome = iota
	Pass
	Fail
	Inconclusive
	// Flaky is a special value that, when used as a test expectation, indicates
	// that we should accept any outcome as expected. Any expectation marked
	// as flaky should have an accompanying bug.
	// TODO(https://fxbug.dev/98306): Align how flaky test cases are handled with
	// the rest of Fuchsia tests.
	Flaky
)

func (o Outcome) String() string {
	switch o {
	case Pass:
		return "Passed"
	case Fail:
		return "!FAILED!"
	case Inconclusive:
		return "!INCONCLUSIVE!"
	case Flaky:
		return "Flaky"
	default:
		panic(fmt.Sprintf("unrecognized Outcome %d", o))
	}
}
