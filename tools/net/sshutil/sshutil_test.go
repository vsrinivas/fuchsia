// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package sshutil

import (
	"errors"
	"fmt"
	"testing"

	"golang.org/x/crypto/ssh"
)

func TestConnectionError(t *testing.T) {
	if IsConnectionError(errors.New("not a connection error")) {
		t.Errorf("IsConnectionError returned true for a non-ConnectionError")
	}

	wrapped := new(ssh.ExitMissingError)
	connErr := ConnectionError{Err: wrapped}
	if !IsConnectionError(connErr) {
		t.Errorf("IsConnectionError returned false for a ConnectionError")
	}
	var eme *ssh.ExitMissingError
	if !errors.As(connErr, &eme) {
		t.Errorf("connErr should wrap ssh.ExitMissingError")
	}

	wrapper := fmt.Errorf("something failed: %w", connErr)
	if !IsConnectionError(wrapper) {
		t.Errorf("a wrapped ConnectionError should still be considered a ConnectionError")
	}
}
