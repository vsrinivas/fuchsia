// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import (
	"testing"
)

func TestStringInLogCheck(t *testing.T) {
	const killerString = "KILLER STRING"
	const exceptString = "Don't die!"
	c := stringInLogCheck{String: killerString, ExceptString: exceptString, Log: SerialLogType}
	gotName := c.Name()
	wantName := "string_in_log/serial_log/KILLER_STRING"
	if gotName != wantName {
		t.Errorf("c.Name() returned %q, want %q", gotName, wantName)
	}
	shouldMatch := TestingOutputs{
		SerialLog: []byte("PREFIX KILLER STRING SUFFIX"),
	}
	if !c.Check(&shouldMatch) {
		t.Errorf("c.Check(%q) returned false, expected true", string(shouldMatch.SerialLog))
	}
	shouldNotMatch := TestingOutputs{
		SerialLog:      []byte("gentle string"),
		SwarmingOutput: []byte(killerString),
	}
	if c.Check(&shouldNotMatch) {
		t.Errorf("c.Check(%q) returned true, expected false", string(shouldNotMatch.SerialLog))
	}
	exceptShouldNotMatch := TestingOutputs{
		SerialLog: []byte(killerString + exceptString),
	}
	if c.Check(&shouldNotMatch) {
		t.Errorf("c.Check(%q) returned true, expected false", string(exceptShouldNotMatch.SerialLog))
	}
}
