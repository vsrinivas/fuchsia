// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package fastboot

import (
	"context"
	"strings"
	"testing"
	"time"
)

func TestErrorContainsStderr(t *testing.T) {
	// Use any binary that will fail and print to stderr when given unexpected arguments.
	// Using something that should be in PATH on any POSIX system:
	// http://pubs.opengroup.org/onlinepubs/9699919799/utilities/contents.html
	shD := Fastboot{"date"}
	_, err := shD.Continue(context.Background())
	if err == nil {
		t.Error("Expected an error")
	}
	if !strings.Contains(err.Error(), "date:") {
		t.Errorf("Expected the standard error text in the error, got: %v", err)
	}
}

func TestRespectsContext(t *testing.T) {
	// Use cat because it doesn't terminate on its own.
	// Using something that should be in PATH on any POSIX system:
	// http://pubs.opengroup.org/onlinepubs/9699919799/utilities/contents.html
	shD := Fastboot{"cat"}
	ctx, cancel := context.WithTimeout(context.Background(), time.Microsecond)
	defer cancel()
	_, err := shD.Continue(ctx)
	if err == nil {
		t.Error("Expected a timeout error, got no error")
	}
	if !strings.Contains(err.Error(), "deadline") {
		t.Errorf("Expected a timeout error, got: %v", err)
	}
}
