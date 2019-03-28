// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package gcs

import (
	"context"

	"fmt"

	"cloud.google.com/go/storage"
	"go.chromium.org/luci/auth"
	"google.golang.org/api/option"
)

// NewClient returns a storage.Client that uses LUCI auth with silent login by default.
// The caller should set the required GCE scopes on the given auth.Options, otherwise
// RPCs to GCS will fail with 403 errors even if the given user has permissions to access
// GCS.
func NewClient(ctx context.Context, opts auth.Options) (*storage.Client, error) {
	return NewClientWithLoginMode(ctx, auth.SilentLogin, opts)
}

// NewClientWithLogin mode returns a storage.Client that uses LUCI auth.
func NewClientWithLoginMode(ctx context.Context, mode auth.LoginMode, opts auth.Options) (*storage.Client, error) {
	authenticator := auth.NewAuthenticator(ctx, mode, opts)
	source, err := authenticator.TokenSource()
	if err != nil {
		return nil, fmt.Errorf("failed to create token source: %v", err)
	}
	client, err := storage.NewClient(ctx, option.WithTokenSource(source))
	if err != nil {
		return nil, fmt.Errorf("failed to create Cloud Storage client: %v", err)
	}
	return client, nil
}
