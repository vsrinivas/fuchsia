// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package resultstore

// TODO(kjharland): Make ToResultStoreXXX methods priviate.
// TODO(kjharland): Make Name properties read-only.

import (
	"log"
	"time"

	"github.com/golang/protobuf/ptypes"
	"github.com/golang/protobuf/ptypes/duration"
	"github.com/golang/protobuf/ptypes/timestamp"
	api "google.golang.org/genproto/googleapis/devtools/resultstore/v2"
)

const (
	// The filename to use for a test's log file. Result Store recognizes this file name
	// and displays it by default when a target is opened in the UI.
	testLogName = "test.log"
)

// Invocation maps to a ResultStore Invocation.  An invocation typically represents the
// result of running some command or tool.
//
// See https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/invocation.proto
// for more documentation.
type Invocation struct {
	// This Invocation's name in Resultstore, set by the back end after creation.
	Name string

	// The unique ID of this Invocation, defined by the user.
	ID string

	// The Cloud Project ID to associate this Invocation with.
	ProjectID string

	// The list of users in the command chain.  The first user in this sequence
	// is the one who instigated the first command in the chain.
	Users []string

	// Labels to categorize this invocation.
	Labels []string

	// Arbitrary name-value pairs. Multiple properties are allowed with the same key.
	// TODO(kjharland): Convert this to a list of `key=value` to support duplicate keys.
	Properties map[string]string

	// A URL pointing to a UI containing more information
	LogURL string

	Status    Status
	StartTime time.Time
	Duration  time.Duration
}

func (i Invocation) ToResultStoreInvocation() *api.Invocation {
	output := &api.Invocation{
		Name: i.Name,
		StatusAttributes: &api.StatusAttributes{
			Status: i.Status.ToResultStoreStatus(),
		},
		InvocationAttributes: &api.InvocationAttributes{
			ProjectId: i.ProjectID,
			Users:     i.Users,
			Labels:    i.Labels,
		},
		Files: []*api.File{
			&api.File{Uid: "invocation.log", Uri: i.LogURL},
		},
		Properties: mapToProperties(i.Properties),
	}

	// Leave ID nil if unspecified since resultstore throws an error when this field is
	// set and does not match the original ID supplied when creating the Invocation.
	if i.ID != "" {
		output.Id = &api.Invocation_Id{
			InvocationId: i.ID,
		}
	}

	// Leave timing attributes nil if unspecified since resultstore throws an error when
	// timing data contains invalid values such as Go's zero-value for time.Time.
	if (i.StartTime != time.Time{}) || i.Duration != 0 {
		output.Timing = &api.Timing{
			StartTime: timeToProtoTimestamp(i.StartTime),
			Duration:  durationToProtoDuration(i.Duration),
		}
	}

	return output
}

func (i *Invocation) FromResultStoreInvocation(input *api.Invocation) {
	*i = Invocation{
		Name: input.GetName(),
		ID:   input.GetId().InvocationId,
	}
}

// Configuration maps to a ResultStore Configuration.
//
// See https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/configuration.proto
// for more documentation.
type Configuration struct {
	// This Configuration's name in Resultstore, set by the back end after creation.
	Name string

	// The unique ID of this Invocation, defined by the user.
	ID string

	// The unique ID of the Invocation to associate this Configuration with.
	InvocationID string

	// Arbitrary name-value pairs. Multiple properties are allowed with the same key.
	// TODO(kjharland): Convert this to a list of `key=value` to support duplicate keys.
	Properties map[string]string
}

func (c Configuration) ToResultStoreConfiguration() *api.Configuration {
	return &api.Configuration{
		Name: c.Name,
		Id: &api.Configuration_Id{
			InvocationId:    c.InvocationID,
			ConfigurationId: c.ID,
		},
		Properties: mapToProperties(c.Properties),
	}
}

func (c *Configuration) FromResultStoreConfiguration(input *api.Configuration) {
	*c = Configuration{
		Name:         input.GetName(),
		ID:           input.GetId().ConfigurationId,
		InvocationID: input.GetId().InvocationId,
	}
}

// Target maps to a ResultStore Target.
//
// See https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/target.proto
// for more documentation.
type Target struct {
	// This Target's name in Resultstore, set by the back end after creation.
	Name string

	// The unique ID of this Target, defined by the user.
	ID *TargetID

	// Arbitrary name-value pairs. Multiple properties are allowed with the same key.
	// TODO(kjharland): Convert this to a list of `key=value` to support duplicate keys.
	Properties map[string]string

	Status    Status
	StartTime time.Time
	Duration  time.Duration

	// The cloud storage link to this test's log.  The URL must have the form:
	// gs://<bucket>/<object>.
	TestLogURI string
}
type TargetID struct {
	ID           string
	InvocationID string
}

func (t Target) ToResultStoreTarget() *api.Target {
	var id *api.Target_Id
	if t.ID != nil {
		id = &api.Target_Id{
			InvocationId: t.ID.InvocationID,
			TargetId:     t.ID.ID,
		}
	}

	return &api.Target{
		Name: t.Name,
		Id:   id,
		Timing: &api.Timing{
			StartTime: timeToProtoTimestamp(t.StartTime),
			Duration:  durationToProtoDuration(t.Duration),
		},
		StatusAttributes: &api.StatusAttributes{
			Status: t.Status.ToResultStoreStatus(),
		},
		Properties: mapToProperties(t.Properties),
		Visible:    true,
		Files: []*api.File{
			&api.File{
				Uid: testLogName,
				Uri: t.TestLogURI,
			},
		},
	}
}

func (t *Target) FromResultStoreTarget(input *api.Target) {
	*t = Target{
		Name: input.GetName(),
		ID: &TargetID{
			ID:           input.GetId().TargetId,
			InvocationID: input.GetId().InvocationId,
		},
	}
}

// ConfiguredTarget maps to a ResultStore ConfiguredTarget.
//
// See https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/configured_target.proto
// for more documentation.
type ConfiguredTarget struct {
	// This ConfiguredTarget's name in Resultstore, set by the back end after creation.
	Name string

	// The unique ID of this ConfiguredTarget, defined by the user.
	ID *ConfiguredTargetID

	// Arbitrary name-value pairs. Multiple properties are allowed with the same key.
	// TODO(kjharland): Convert this to a list of `key=value` to support duplicate keys.
	Properties map[string]string

	Status    Status
	StartTime time.Time
	Duration  time.Duration
}
type ConfiguredTargetID struct {
	InvocationID string
	TargetID     string
	ConfigID     string
}

func (t ConfiguredTarget) ToResultStoreConfiguredTarget() *api.ConfiguredTarget {
	var id *api.ConfiguredTarget_Id
	if t.ID != nil {
		id = &api.ConfiguredTarget_Id{
			InvocationId:    t.ID.InvocationID,
			TargetId:        t.ID.TargetID,
			ConfigurationId: t.ID.ConfigID,
		}
	}

	return &api.ConfiguredTarget{
		Name: t.Name,
		Id:   id,
		StatusAttributes: &api.StatusAttributes{
			Status: t.Status.ToResultStoreStatus(),
		},
		Timing: &api.Timing{
			StartTime: timeToProtoTimestamp(t.StartTime),
			Duration:  durationToProtoDuration(t.Duration),
		},
		Properties: mapToProperties(t.Properties),
	}
}

func (t *ConfiguredTarget) FromResultStoreConfiguredTarget(input *api.ConfiguredTarget) {
	*t = ConfiguredTarget{
		Name: input.GetName(),
		ID: &ConfiguredTargetID{
			InvocationID: input.GetId().InvocationId,
			TargetID:     input.GetId().TargetId,
			ConfigID:     input.GetId().ConfigurationId,
		},
	}
}

// TestAction maps to a ResultStore Action with a child TestAction.
//
// See https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/action.proto
// for more documentation.
type TestAction struct {
	// This Action's name in Resultstore, set by the back end after creation.
	Name string

	// The unique ID of this Action, defined by the user.
	ID *TestActionID

	// The name of the test suite exercised by this action.
	TestSuite string

	// The cloud storage link to this test's log.  The URL must have the form:
	// gs://<bucket>/<object>.
	TestLogURI string

	Status    Status
	StartTime time.Time
	Duration  time.Duration
}
type TestActionID struct {
	ID           string
	InvocationID string
	TargetID     string
	ConfigID     string
}

func (a TestAction) ToResultStoreAction() *api.Action {
	var id *api.Action_Id
	if a.ID != nil {
		id = &api.Action_Id{
			InvocationId:    a.ID.InvocationID,
			TargetId:        a.ID.TargetID,
			ConfigurationId: a.ID.ConfigID,
			ActionId:        a.ID.ID,
		}
	}
	return &api.Action{
		Name: a.Name,
		Id:   id,
		StatusAttributes: &api.StatusAttributes{
			Status: a.Status.ToResultStoreStatus(),
		},
		ActionType: &api.Action_TestAction{
			TestAction: &api.TestAction{
				TestSuite: &api.TestSuite{
					SuiteName: a.TestSuite,
				},
			},
		},
		Files: []*api.File{
			&api.File{
				Uid: testLogName,
				Uri: a.TestLogURI,
			},
		},
		Timing: &api.Timing{
			StartTime: timeToProtoTimestamp(a.StartTime),
			Duration:  durationToProtoDuration(a.Duration),
		},
	}
}

func (a *TestAction) FromResultStoreAction(input *api.Action) {
	*a = TestAction{
		Name: input.GetName(),
		ID: &TestActionID{
			InvocationID: input.GetId().InvocationId,
			TargetID:     input.GetId().TargetId,
			ConfigID:     input.GetId().ConfigurationId,
			ID:           input.GetId().ActionId,
		},
	}
}

// Status describes the status of a ResultStore entity.
//
// See https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/common.proto
// for more documentation.
type Status string

const (
	Building = Status("BUILDING")
	Passed   = Status("PASSED")
	Failed   = Status("FAILED")
	TimedOut = Status("TIMED_OUT")
	Testing  = Status("TESTING")
	Unknown  = Status("Unknown")
)

func (r Status) ToResultStoreStatus() api.Status {
	switch r {
	case Building:
		return api.Status_BUILDING
	case Failed:
		return api.Status_FAILED
	case Passed:
		return api.Status_PASSED
	case Testing:
		return api.Status_TESTING
	case TimedOut:
		return api.Status_TIMED_OUT
	default:
		log.Printf("unknown test status: %v", r)
		return api.Status_UNKNOWN
	}
}

func timeToProtoTimestamp(input time.Time) *timestamp.Timestamp {
	output, err := ptypes.TimestampProto(input)
	if err != nil {
		// We should never get here unless the caller manually created some strange,
		// invalid time.Time value or the ResultStore API returned a nil value.
		panic(err.Error())
	}
	return output
}

func durationToProtoDuration(input time.Duration) *duration.Duration {
	return ptypes.DurationProto(input)
}

func mapToProperties(input map[string]string) []*api.Property {
	var props []*api.Property
	for k, v := range input {
		props = append(props, &api.Property{Key: k, Value: v})
	}
	return props
}
