// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package daemon

import "testing"

func TestGreatestIntFromString(t *testing.T) {
	typicalSet := []string{"0", "1", "2", "3"}
	expectedInt := 3
	expectedStr := "3"
	i, s, err := GreatestIntStr(typicalSet)
	checkResults(typicalSet, err, i, s, expectedInt, expectedStr, t)

	withPadding := []string{"000", "001", "2", "003"}
	expectedInt = 3
	expectedStr = "003"
	i, s, err = GreatestIntStr(withPadding)
	checkResults(withPadding, err, i, s, expectedInt, expectedStr, t)

	jumbled := []string{"3", "1", "2", "0"}
	expectedInt = 3
	expectedStr = "3"
	i, s, err = GreatestIntStr(jumbled)
	checkResults(jumbled, err, i, s, expectedInt, expectedStr, t)

	jumbled2 := []string{"1", "3", "2", "0"}
	expectedInt = 3
	expectedStr = "3"
	i, s, err = GreatestIntStr(jumbled2)
	checkResults(jumbled2, err, i, s, expectedInt, expectedStr, t)

	nan := []string{"1", "2", "red", "blue"}
	i, s, err = GreatestIntStr(nan)
	if err == nil {
		t.Logf("Expected %s to produce NaN error, but no error was produced")
		t.Fail()
	} else if _, ok := err.(ErrNan); !ok {
		t.Logf("Expected %s to produce NaN error, but instead error is %s", nan, err)
		t.Fail()
	}

	empty := []string{}
	i, s, err = GreatestIntStr(empty)
	if err == nil || err != ErrNoInput {
		t.Logf("Expected empty set to produce no input error, but instead error is %s", err)
		t.Fail()
	}
}

func checkResults(strs []string, err error, resultInt int, resultStr string, expectedInt int,
	expectedStr string, t *testing.T) {
	if err != nil || resultInt != expectedInt || expectedStr != resultStr {
		t.Logf("Unexpected result, expected integer %d and string %q, but got integer %d and "+
			"string %s from strings %s had error", resultInt, resultStr, expectedInt, expectedStr,
			strs, err)
		t.Fail()
	}
}
