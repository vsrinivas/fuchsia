// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"context"
	"errors"
	"reflect"
	"testing"

	_struct "github.com/golang/protobuf/ptypes/struct"
	buildbucketpb "go.chromium.org/luci/buildbucket/proto"

	"google.golang.org/genproto/protobuf/field_mask"
	"google.golang.org/grpc"
)

func TestGetStorageBucket(t *testing.T) {
	tests := []struct {
		// The name of this test case.
		name string

		// The ID of the build to query.
		input string

		// A mock RPC response from the BuildBucket service.
		mock mockBuildsClient
		// mock *buildbucketpb.Build

		// The expected GetBuildRequest.
		expectedRequest *buildbucketpb.GetBuildRequest

		// The expected output Cloud Storage bucket.
		expectedBucket string

		// Whether to expect an error.
		expectErr bool
	}{
		{
			name:           "should return the storage bucket read from a build's properties",
			input:          "123",
			expectedBucket: "the_bucket",
			expectedRequest: &buildbucketpb.GetBuildRequest{
				Id: 123,
				Fields: &field_mask.FieldMask{
					Paths: []string{"input"},
				},
			},
			mock: mockBuildsClient{
				response: &buildbucketpb.Build{
					Id: 123,
					Input: &buildbucketpb.Build_Input{
						Properties: &_struct.Struct{
							Fields: map[string]*_struct.Value{
								"gcs_bucket": &_struct.Value{
									Kind: &_struct.Value_StringValue{
										StringValue: "the_bucket",
									},
								},
							},
						},
					},
				},
			},
		}, {
			name:      "should err if no build is returned",
			input:     "123",
			expectErr: true,
			expectedRequest: &buildbucketpb.GetBuildRequest{
				Id: 123,
				Fields: &field_mask.FieldMask{
					Paths: []string{"input"},
				},
			},
			mock: mockBuildsClient{
				response: nil,
			},
		}, {
			name:      "should err if the RPC fails",
			input:     "123",
			expectErr: true,
			expectedRequest: &buildbucketpb.GetBuildRequest{
				Id: 123,
				Fields: &field_mask.FieldMask{
					Paths: []string{"input"},
				},
			},
			mock: mockBuildsClient{
				shouldErr: true,
				response: &buildbucketpb.Build{
					Id: 123,
					Input: &buildbucketpb.Build_Input{
						Properties: &_struct.Struct{
							Fields: map[string]*_struct.Value{},
						},
					},
				},
			},
		}, {
			name:      "should err if no property describing the storage bucket is found",
			input:     "123",
			expectErr: true,
			expectedRequest: &buildbucketpb.GetBuildRequest{
				Id: 123,
				Fields: &field_mask.FieldMask{
					Paths: []string{"input"},
				},
			},
			mock: mockBuildsClient{
				response: &buildbucketpb.Build{
					Id: 123,
					Input: &buildbucketpb.Build_Input{
						Properties: &_struct.Struct{
							Fields: map[string]*_struct.Value{},
						},
					},
				},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			bucket, err := getStorageBucket(context.Background(), &tt.mock, tt.input)
			if err != nil != tt.expectErr {
				if err == nil {
					t.Error("wanted an error but got nil")
				} else {
					t.Errorf("unxpected error: %v", err)
				}
			}

			compare := func(kind string, expected, actual interface{}) {
				if !reflect.DeepEqual(expected, actual) {
					t.Errorf("expected %s:\n%+v\nbut got:\n%+v", kind, expected, actual)
				}
			}

			compare("SearchBuildsRequest", tt.expectedRequest, tt.mock.request)
			compare("Storage bucket", tt.expectedBucket, bucket)
		})
	}
}

type mockBuildsClient struct {
	shouldErr bool
	response  *buildbucketpb.Build
	request   *buildbucketpb.GetBuildRequest
}

func (mock *mockBuildsClient) GetBuild(ctx context.Context, req *buildbucketpb.GetBuildRequest, _ ...grpc.CallOption) (*buildbucketpb.Build, error) {
	mock.request = req
	if mock.shouldErr {
		return nil, errors.New("no builds found")
	}

	return mock.response, nil
}
