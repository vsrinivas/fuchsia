// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package streams

import (
	"context"
	"io"
	"os"
)

type streamKeyType string

const (
	stdoutKey = streamKeyType("stdout")
	stderrKey = streamKeyType("stderr")
)

// Stdout returns os.Stdout or the mocked stdout writer associated with the
// given context.
//
// Use this function in code where you want to test what it writes to os.Stdout.
func Stdout(ctx context.Context) io.Writer {
	return getStream(ctx, stdoutKey, os.Stdout)
}

// Stderr returns os.Stderr or the mocked stderr writer associated with the
// given context.
//
// Use this function in code where you want to test what it writes to os.Stderr.
func Stderr(ctx context.Context) io.Writer {
	return getStream(ctx, stderrKey, os.Stderr)
}

func getStream(ctx context.Context, key streamKeyType, def *os.File) io.Writer {
	if s, ok := ctx.Value(key).(io.Writer); ok && s != nil {
		return s
	}
	return def
}

// ContextWithStdout overrides os.Stdout for all code that uses the returned
// context, as long as it accesses stdout using `streams.Stdout(ctx)` instead of
// using `os.Stdout` directly.
//
// This should only be used in tests; production code should never override
// os.Stdout.
func ContextWithStdout(ctx context.Context, s io.Writer) context.Context {
	return context.WithValue(ctx, stdoutKey, s)
}

// ContextWithStderr overrides os.Stderr for all code that uses the returned
// context, as long as it accesses stderr using `streams.Stderr(ctx)` instead of
// using `os.Stderr` directly.
//
// This should only be used in tests; production code should never override
// os.Stderr.
func ContextWithStderr(ctx context.Context, s io.Writer) context.Context {
	return context.WithValue(ctx, stderrKey, s)
}
