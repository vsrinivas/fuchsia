// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package retry

import (
	"testing"
	"time"
)

func TestZeroBackoff(t *testing.T) {
	backoff := ZeroBackoff{}
	if backoff.Next() != 0 {
		t.Error("invalid interval")
	}
}

func TestConstantBackoff(t *testing.T) {
	backoff := NewConstantBackoff(time.Second)
	if backoff.Next() != time.Second {
		t.Error("invalid interval")
	}
}

func TestMaxTriesBackoff(t *testing.T) {
	backoff := WithMaxRetries(&ZeroBackoff{}, 10)
	for i := 0; i < 10; i++ {
		if backoff.Next() != 0 {
			t.Error("invalid interval")
		}
	}
	if backoff.Next() != Stop {
		t.Error("did not stop")
	}
}
