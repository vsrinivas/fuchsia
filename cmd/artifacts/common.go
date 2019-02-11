// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"context"
	"fmt"
	"strconv"

	"fuchsia.googlesource.com/tools/buildbucket"
	"google.golang.org/genproto/protobuf/field_mask"
	"google.golang.org/grpc"

	buildbucketpb "go.chromium.org/luci/buildbucket/proto"
)

// The name of the recipe property passed to Fuchsia builders, which communicates which
// Cloud storage bucket to upload artifacts to.
const bucketPropertyName = "gcs_bucket"

// buildsClient sends RPCs to a BuildBucket server.
type buildsClient interface {
	GetBuild(context.Context, *buildbucketpb.GetBuildRequest, ...grpc.CallOption) (*buildbucketpb.Build, error)
}

func getStorageBucket(ctx context.Context, client buildsClient, build string) (string, error) {
	buildID, err := strconv.ParseInt(build, 10, 64)
	if err != nil {
		return "", err
	}

	response, err := client.GetBuild(ctx, &buildbucketpb.GetBuildRequest{
		Id: buildID,
		Fields: &field_mask.FieldMask{
			Paths: []string{"input"},
		},
	})

	if err != nil {
		return "", err
	}

	if response == nil {
		return "", fmt.Errorf("build %s not found", build)
	}

	wrapper := buildbucket.Build(*response)
	property, ok := wrapper.Property(bucketPropertyName)
	if !ok {
		return "", fmt.Errorf("no property %q found for build %q", bucketPropertyName, build)
	}

	return property.StringValue(), nil
}
