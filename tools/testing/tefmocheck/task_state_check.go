package tefmocheck

import (
	"path"
)

// taskStateCheck checks if the swarming task is in State.
type taskStateCheck struct {
	State string
}

func (c taskStateCheck) Check(to *TestingOutputs) bool {
	return to.SwarmingSummary.Results.State == c.State
}

func (c taskStateCheck) Name() string {
	return path.Join("task_state", c.State)
}

// taskFailureCheck checks if the swarming task failed.
type taskFailureCheck struct{}

func (c taskFailureCheck) Check(to *TestingOutputs) bool {
	return to.SwarmingSummary.Results.State == "COMPLETED" && to.SwarmingSummary.Results.Failure
}

func (c taskFailureCheck) Name() string {
	return "task_failure"
}

// taskInternalFailureCheck checks if the swarming task internally failed.
type taskInternalFailureCheck struct{}

func (c taskInternalFailureCheck) Check(to *TestingOutputs) bool {
	return to.SwarmingSummary.Results.State == "COMPLETED" && to.SwarmingSummary.Results.InternalFailure
}

func (c taskInternalFailureCheck) Name() string {
	return "task_internal_failure"
}

// TaskStateChecks contains checks to cover every possible state.
// A task can only be in one state, so their relative order doesn't matter.
var TaskStateChecks []FailureModeCheck = []FailureModeCheck{
	// Covers state == COMPLETED.
	taskInternalFailureCheck{},
	taskFailureCheck{},
	// All other states.
	taskStateCheck{State: "BOT_DIED"},
	taskStateCheck{State: "CANCELED"},
	taskStateCheck{State: "EXPIRED"},
	taskStateCheck{State: "INVALID"},
	taskStateCheck{State: "KILLED"},
	taskStateCheck{State: "NO_RESOURCE"},
	taskStateCheck{State: "PENDING"},
	taskStateCheck{State: "RUNNING"},
	taskStateCheck{State: "TIMED_OUT"},
}
