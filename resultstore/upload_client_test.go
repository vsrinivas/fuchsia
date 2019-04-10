// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package resultstore

import (
	"context"
	"reflect"
	"testing"
	"time"

	"fuchsia.googlesource.com/tools/resultstore/mocks"
	"github.com/golang/mock/gomock"
	api "google.golang.org/genproto/googleapis/devtools/resultstore/v2"
	"google.golang.org/genproto/protobuf/field_mask"
)

var (
	// The value of entity start times and durations is not important for these tests,
	// so we use the same value everywhere.
	may18_1993 = time.Date(1993, time.May, 18, 0, 0, 0, 0, time.UTC)
)

const (
	// The specific values of the UUIDs are not important for these tests. We just want to
	// verify that the client sets some value other than the empty string, so we just use
	// the same UUID everywhere.
	testUUID = "uuid"

	// The specific value of the auth token included in requests is not important. What
	// matters is that the value in an RPC matches the value given to the UploadClient.
	// Reuse this same value in all test cases.
	testAuthToken = "auth_token"
)

type tester struct {
	client *UploadClient
	mock   *mocks.MockResultStoreUploadClient
}

func TestUploadClient(t *testing.T) {
	tests := []struct {
		// The method being tested. Keep tests ordered alphabetically by method name.
		method string

		// A brief test case description.
		description string

		// Exercises the method under test.
		execute func(ctx context.Context, tester *tester) (interface{}, error)

		// The expected output of the method
		output interface{}
	}{
		{
			method:      "CreateConfiguration",
			description: "should rmake an RPC to create a Configuration",
			output: &Configuration{
				Name:         "resultstore_configuration_name",
				ID:           "configuration_id",
				InvocationID: "invocation_id",
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &Configuration{
					ID:           "configuration_id",
					InvocationID: "invocation_id",
					Properties:   map[string]string{"key": "value"},
				}

				response := &api.Configuration{
					Name: "resultstore_configuration_name",
					Id: &api.Configuration_Id{
						ConfigurationId: "configuration_id",
						InvocationId:    "invocation_id",
					},
				}

				tester.mock.EXPECT().
					CreateConfiguration(ctx, &api.CreateConfigurationRequest{
						RequestId:          testUUID,
						AuthorizationToken: testAuthToken,
						Parent:             "invocation_name",
						ConfigId:           "configuration_id",
						Configuration:      input.ToResultStoreConfiguration(),
					}).
					Return(response, nil)

				return tester.client.CreateConfiguration(ctx, input, "invocation_name")
			},
		},
		{
			method:      "CreateConfiguredTarget",
			description: "should make an RPC to create a ConfiguredTarget",
			output: &ConfiguredTarget{
				Name: "resultstore_configured_target_name",
				ID: &ConfiguredTargetID{
					InvocationID: "invocation_id",
					ConfigID:     "configuration_id",
					TargetID:     "target_id",
				},
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &ConfiguredTarget{
					ID: &ConfiguredTargetID{
						InvocationID: "invocation_id",
						TargetID:     "target_id",
						ConfigID:     "configuration_id",
					},
					Properties: map[string]string{"key": "value"},
					StartTime:  may18_1993,
					Status:     Passed,
				}

				response := &api.ConfiguredTarget{
					Name: "resultstore_configured_target_name",
					Id: &api.ConfiguredTarget_Id{
						InvocationId:    "invocation_id",
						ConfigurationId: "configuration_id",
						TargetId:        "target_id",
					},
				}

				tester.mock.EXPECT().
					CreateConfiguredTarget(ctx, &api.CreateConfiguredTargetRequest{
						RequestId:          testUUID,
						AuthorizationToken: testAuthToken,
						Parent:             "target_name",
						ConfigId:           input.ID.ConfigID,
						ConfiguredTarget:   input.ToResultStoreConfiguredTarget(),
					}).
					Return(response, nil)

				return tester.client.CreateConfiguredTarget(ctx, input, "target_name")
			},
		},
		{
			method:      "CreateInvocation",
			description: "should make an RPC to create an Invocation",
			output: &Invocation{
				Name: "resultstore_invocation_name",
				ID:   "invocation_id",
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &Invocation{
					ProjectID:  "123456789",
					ID:         "invocation_id",
					Users:      []string{"user"},
					Labels:     []string{"label"},
					Properties: map[string]string{"key": "value"},
					LogURL:     "http://test.log",
					StartTime:  may18_1993,
					Status:     Passed,
				}

				response := &api.Invocation{
					Name: "resultstore_invocation_name",
					Id: &api.Invocation_Id{
						InvocationId: "invocation_id",
					},
				}

				tester.mock.EXPECT().
					CreateInvocation(ctx, &api.CreateInvocationRequest{
						RequestId:          testUUID,
						AuthorizationToken: testAuthToken,
						InvocationId:       "invocation_id",
						Invocation:         input.ToResultStoreInvocation(),
					}).
					Return(response, nil)

				return tester.client.CreateInvocation(ctx, input)
			},
		},
		{
			method:      "CreateTarget",
			description: "should make an RPC to create a Target",
			output: &Target{
				Name: "resultstore_target_name",
				ID: &TargetID{
					ID:           "target_id",
					InvocationID: "invocation_id",
				},
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &Target{
					ID: &TargetID{
						ID: "target_id",
					},
					Properties: map[string]string{"key": "value"},
					StartTime:  may18_1993,
					Status:     Passed,
				}

				response := &api.Target{
					Name: "resultstore_target_name",
					Id: &api.Target_Id{
						TargetId:     "target_id",
						InvocationId: "invocation_id",
					},
				}

				tester.mock.EXPECT().
					CreateTarget(ctx, &api.CreateTargetRequest{
						RequestId:          testUUID,
						AuthorizationToken: testAuthToken,
						Parent:             "invocation_name",
						TargetId:           input.ID.ID,
						Target:             input.ToResultStoreTarget(),
					}).
					Return(response, nil)

				return tester.client.CreateTarget(ctx, input, "invocation_name")
			},
		},
		{
			method:      "CreateTestAction",
			description: "should make an RPC to create a Test Action",
			output: &TestAction{
				Name: "resultstore_action_name",
				ID: &TestActionID{
					InvocationID: "invocation_id",
					ConfigID:     "configuration_id",
					TargetID:     "target_id",
				},
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &TestAction{
					ID: &TestActionID{
						ID:           "test",
						InvocationID: "invocation_id",
						TargetID:     "target_id",
						ConfigID:     "configuration_id",
					},
					TestSuite:  "test_suite",
					TestLogURI: "http://test.log",
					StartTime:  may18_1993,
					Status:     Passed,
				}

				response := &api.Action{
					Name: "resultstore_action_name",
					Id: &api.Action_Id{
						InvocationId:    "invocation_id",
						ConfigurationId: "configuration_id",
						TargetId:        "target_id",
					},
				}

				tester.mock.EXPECT().
					CreateAction(ctx, &api.CreateActionRequest{
						RequestId:          testUUID,
						AuthorizationToken: testAuthToken,
						Parent:             "configured_target_name",
						ActionId:           "test",
						Action:             input.ToResultStoreAction(),
					}).
					Return(response, nil)

				return tester.client.CreateTestAction(ctx, input, "configured_target_name")

			},
		},
		{
			method:      "FinishConfiguredTarget",
			description: "should make an RPC to finish a ConfiguredTarget",
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				tester.mock.EXPECT().
					FinishConfiguredTarget(ctx, &api.FinishConfiguredTargetRequest{
						AuthorizationToken: testAuthToken,
						Name:               "configured_target_name",
					}).
					Return(&api.FinishConfiguredTargetResponse{}, nil)

				return nil, tester.client.FinishConfiguredTarget(ctx, "configured_target_name")
			},
		},
		{
			method:      "FinishInvocation",
			description: "should make an RPC to finish an Invocation",
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				tester.mock.EXPECT().
					FinishInvocation(ctx, &api.FinishInvocationRequest{
						AuthorizationToken: testAuthToken,
						Name:               "invocation_name",
					}).
					Return(&api.FinishInvocationResponse{}, nil)

				return nil, tester.client.FinishInvocation(ctx, "invocation_name")
			},
		},
		{
			method:      "FinishTarget",
			description: "should make an RPC to finish a Target",
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				tester.mock.EXPECT().
					FinishTarget(ctx, &api.FinishTargetRequest{
						AuthorizationToken: testAuthToken,
						Name:               "target_name",
					}).
					Return(&api.FinishTargetResponse{}, nil)

				return nil, tester.client.FinishTarget(ctx, "target_name")
			},
		},
		{
			method:      "UpdateConfiguredTarget",
			description: "should make an RPC to update a ConfiguredTarget",
			output: &ConfiguredTarget{
				Name: "resultstore_configured_target_name",
				ID: &ConfiguredTargetID{
					InvocationID: "invocation_id",
					ConfigID:     "configuration_id",
					TargetID:     "target_id",
				},
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &ConfiguredTarget{
					ID: &ConfiguredTargetID{
						InvocationID: "invocation_id",
						TargetID:     "target_id",
						ConfigID:     "configuration_id",
					},
					Properties: map[string]string{"key": "value"},
					Status:     Passed,
					StartTime:  may18_1993,
					Duration:   time.Hour,
				}

				response := &api.ConfiguredTarget{
					Name: "resultstore_configured_target_name",
					Id: &api.ConfiguredTarget_Id{
						InvocationId:    "invocation_id",
						ConfigurationId: "configuration_id",
						TargetId:        "target_id",
					},
				}

				fieldsToUpdate := []string{"timing.duration", "status_attributes"}

				tester.mock.EXPECT().
					UpdateConfiguredTarget(ctx, &api.UpdateConfiguredTargetRequest{
						AuthorizationToken: testAuthToken,
						ConfiguredTarget:   input.ToResultStoreConfiguredTarget(),
						UpdateMask:         &field_mask.FieldMask{Paths: fieldsToUpdate},
					}).
					Return(response, nil)

				return tester.client.UpdateConfiguredTarget(ctx, input, fieldsToUpdate)
			},
		},
		{
			method:      "UpdateInvocation",
			description: "should make an RPC to update an Invocation",
			output: &Invocation{
				Name: "resultstore_invocation_name",
				ID:   "invocation_id",
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &Invocation{
					ID:         "invocation_id",
					Properties: map[string]string{"key": "value"},
					ProjectID:  "project_id",
					StartTime:  may18_1993,
					Duration:   time.Hour,
					Users:      []string{"users"},
					Labels:     []string{"label"},
					LogURL:     "url",
					Status:     Passed,
				}

				response := &api.Invocation{
					Name: "resultstore_invocation_name",
					Id: &api.Invocation_Id{
						InvocationId: "invocation_id",
					},
				}

				fieldsToUpdate := []string{"timing.duration", "status_attributes"}

				tester.mock.EXPECT().
					UpdateInvocation(ctx, &api.UpdateInvocationRequest{
						AuthorizationToken: testAuthToken,
						Invocation:         input.ToResultStoreInvocation(),
						UpdateMask:         &field_mask.FieldMask{Paths: fieldsToUpdate},
					}).
					Return(response, nil)

				return tester.client.UpdateInvocation(ctx, input, fieldsToUpdate)
			},
		},
		{
			method:      "UpdateTarget",
			description: "should make an RPC to update a Target",
			output: &Target{
				Name: "resultstore_target_name",
				ID: &TargetID{
					ID:           "target_id",
					InvocationID: "invocation_id",
				},
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &Target{
					ID: &TargetID{
						ID:           "target_id",
						InvocationID: "invocation_id",
					},
					Properties: map[string]string{"key": "value"},
					StartTime:  may18_1993,
					Duration:   time.Hour,
					Status:     Passed,
				}

				response := &api.Target{
					Name: "resultstore_target_name",
					Id: &api.Target_Id{
						TargetId:     "target_id",
						InvocationId: "invocation_id",
					},
				}

				fieldsToUpdate := []string{"timing.duration", "status_attributes"}

				tester.mock.EXPECT().
					UpdateTarget(ctx, &api.UpdateTargetRequest{
						AuthorizationToken: testAuthToken,
						Target:             input.ToResultStoreTarget(),
						UpdateMask:         &field_mask.FieldMask{Paths: fieldsToUpdate},
					}).
					Return(response, nil)

				return tester.client.UpdateTarget(ctx, input, fieldsToUpdate)
			},
		},
		{
			method:      "UpdateTestAction",
			description: "should make an RPC to update a Test Action",
			output: &TestAction{
				Name: "resultstore_action_name",
				ID: &TestActionID{
					ID:           "action_id",
					InvocationID: "invocation_id",
					ConfigID:     "configuration_id",
					TargetID:     "target_id",
				},
			},
			execute: func(ctx context.Context, tester *tester) (interface{}, error) {
				input := &TestAction{
					ID: &TestActionID{
						ID:           "test",
						InvocationID: "invocation_id",
						TargetID:     "target_id",
						ConfigID:     "configuration_id",
					},
					TestSuite:  "test_suite",
					TestLogURI: "http://test.log",
					StartTime:  may18_1993,
					Duration:   time.Hour,
					Status:     Passed,
				}

				response := &api.Action{
					Name: "resultstore_action_name",
					Id: &api.Action_Id{
						ActionId:        "action_id",
						InvocationId:    "invocation_id",
						ConfigurationId: "configuration_id",
						TargetId:        "target_id",
					},
				}

				fieldsToUpdate := []string{"timing.duration", "status_attributes"}

				tester.mock.EXPECT().
					UpdateAction(ctx, &api.UpdateActionRequest{
						AuthorizationToken: testAuthToken,
						Action:             input.ToResultStoreAction(),
						UpdateMask:         &field_mask.FieldMask{Paths: fieldsToUpdate},
					}).
					Return(response, nil)

				return tester.client.UpdateTestAction(ctx, input, fieldsToUpdate)
			},
		},
	}

	setup := func(t *testing.T) (context.Context, *UploadClient, *mocks.MockResultStoreUploadClient, *gomock.Controller) {
		ctx, err := SetTestUUID(context.Background(), testUUID)
		if err != nil {
			t.Fatalf("failed to set test uuid: %v", err)
		}

		ctx, err = SetAuthToken(ctx, testAuthToken)
		if err != nil {
			t.Fatalf("failed to set test auth token: %v", err)
		}

		controller := gomock.NewController(t)
		mock := mocks.NewMockResultStoreUploadClient(controller)
		client := &UploadClient{client: mock}
		return ctx, client, mock, controller
	}

	for _, tt := range tests {
		t.Run(tt.method, func(t *testing.T) {
			t.Run(tt.description, func(t *testing.T) {
				ctx, client, mock, controller := setup(t)
				defer controller.Finish()

				actual, err := tt.execute(ctx, &tester{client: client, mock: mock})
				if err != nil {
					t.Errorf("call to %q erred: %v", tt.method, err)
					return
				}

				expected := tt.output
				if !reflect.DeepEqual(expected, actual) {
					t.Errorf("objects do not match:\n")
					t.Errorf("Expected: %+v\n", expected)
					t.Errorf("Actual:   %+v\n", actual)
				}
			})
		})
	}
}
