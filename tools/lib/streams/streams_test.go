// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package streams

import (
	"bytes"
	"context"
	"os"
	"testing"
)

func TestStdout(t *testing.T) {
	t.Run("real stdout", func(t *testing.T) {
		ctx := context.Background()
		stdout := Stdout(ctx)
		if stdout != os.Stdout {
			t.Errorf("Expected Stdout to be os.Stdout, got %+v", stdout)
		}
	})

	t.Run("fake stdout", func(t *testing.T) {
		buf := bytes.NewBuffer(nil)
		ctx := ContextWithStdout(context.Background(), buf)
		stdout := Stdout(ctx)
		if stdout != buf {
			t.Errorf("Expected Stdout to be a buffer, got %+v", stdout)
		}
	})
}

func TestStderr(t *testing.T) {
	t.Run("real stderr", func(t *testing.T) {
		ctx := context.Background()
		stderr := Stderr(ctx)
		if stderr != os.Stderr {
			t.Errorf("Expected Stderr to be os.Stderr, got %+v", stderr)
		}
	})

	t.Run("fake stderr", func(t *testing.T) {
		buf := bytes.NewBuffer(nil)
		ctx := ContextWithStderr(context.Background(), buf)
		stderr := Stderr(ctx)
		if stderr != buf {
			t.Errorf("Expected Stderr to be a buffer, got %+v", stderr)
		}
	})
}
