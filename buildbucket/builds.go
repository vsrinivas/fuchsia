// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package buildbucket

import (
	"context"
	"fmt"

	"go.chromium.org/luci/auth"
	buildbucketpb "go.chromium.org/luci/buildbucket/proto"
	"go.chromium.org/luci/grpc/prpc"
)

// DefaultHost is the default Buildbucket server.
const DefaultHost = "cr-buildbucket.appspot.com"

// NewBuildsClient returns a new BuildsClient.
func NewBuildsClient(ctx context.Context, host string, opts auth.Options) (buildbucketpb.BuildsClient, error) {
	authenticator := auth.NewAuthenticator(ctx, auth.SilentLogin, opts)
	httpClient, err := authenticator.Client()
	if err != nil {
		return nil, fmt.Errorf("failed to get authenticated http client: %v", err)
	}

	return buildbucketpb.NewBuildsPRPCClient(&prpc.Client{
		C:    httpClient,
		Host: host,
	}), nil
}
