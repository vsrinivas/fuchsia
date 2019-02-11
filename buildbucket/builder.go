// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package buildbucket

import (
	"flag"
	"fmt"
	"strings"

	buildbucketpb "go.chromium.org/luci/buildbucket/proto"
)

// builderIDFlag identifies a Builder. This is a convenience type for reading a builder id
// from command line flags.
type builderIDFlag buildbucketpb.BuilderID

func (b builderIDFlag) String() string {
	return fmt.Sprintf("%s/%s/%s", b.Project, b.Bucket, b.Builder)
}

// Set implements flag.Value
func (b *builderIDFlag) Set(input string) error {
	parts := strings.SplitN(input, "/", 3)
	if len(parts) != 3 {
		return fmt.Errorf("invalid builder: %s must have form 'project/bucket/builder'", input)
	}

	b.Project = parts[0]
	b.Bucket = parts[1]
	b.Builder = parts[2]
	return nil
}

// Get returns the parsed flag value. The output can be cast as a buildbucketpb.BuilderID.
func (b builderIDFlag) Get() interface{} {
	return buildbucketpb.BuilderID(b)
}

// BuilderID returns a flag.Value for reading a builder ID from a string.  The format of
// the input is project/bucket/build.
func BuilderID() flag.Getter {
	return &builderIDFlag{}
}
