// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tefmocheck

import "net/url"

// SwarmingTaskSummary is the summary of an individual task found in the output of
// `swarming collect`.
type SwarmingTaskSummary struct {
	Outputs []string                `json:"outputs,omitempty"`
	Results *SwarmingRpcsTaskResult `json:"results"`
	Host    string
}

// TaskURL returns the Swarming UI URL for the task.
func (sts *SwarmingTaskSummary) TaskURL() string {
	if sts.Host == "" {
		return "unknown"
	}
	u := url.URL{
		Scheme:   "https",
		Host:     sts.Host,
		Path:     "task",
		RawQuery: "id=" + sts.Results.TaskId,
	}
	return u.String()
}

// BotURL returns the Swarming UI URL for the bot the task ran on.
func (sts *SwarmingTaskSummary) BotURL() string {
	if sts.Host == "" {
		return "unknown"
	}
	u := url.URL{
		Scheme:   "https",
		Host:     sts.Host,
		Path:     "bot",
		RawQuery: "id=" + sts.Results.BotId,
	}
	return u.String()
}

// SwarmingRpcsTaskResult is the result of a single task execution.
// This is based on the type of the same name in
// go.chromium.org/luci/common/api/swarming/swarming/v1, but we include only the fields
// we care about.
type SwarmingRpcsTaskResult struct {
	// BotDimensions: Represents a mapping of string to list of strings.
	BotDimensions    []*SwarmingRpcsStringListPair `json:"bot_dimensions,omitempty"`
	BotId            string                        `json:"bot_id,omitempty"`
	BotVersion       string                        `json:"bot_version,omitempty"`
	CurrentTaskSlice int64                         `json:"current_task_slice,omitempty,string"`
	DedupedFrom      string                        `json:"deduped_from,omitempty"`
	Duration         float64                       `json:"duration,omitempty"`
	ExitCode         int64                         `json:"exit_code,omitempty,string"`
	Failure          bool                          `json:"failure,omitempty"`
	InternalFailure  bool                          `json:"internal_failure,omitempty"`
	Name             string                        `json:"name,omitempty"`
	// OutputsRef: Defines a data tree reference for Swarming task inputs or
	// outputs. It can either be: - a reference to an isolated file on an
	// isolate server - a reference to an isolated file on a RBE CAS server
	// In the RBE CAS case, the isolatedserver must be set to GCP name, and
	// namespace must be set to "sha256-GCP". For the moment, RBE CAS
	// requires SHA-256 and doesn't support precompressed data.
	OutputsRef     *SwarmingRpcsFilesRef `json:"outputs_ref,omitempty"`
	RunId          string                `json:"run_id,omitempty"`
	ServerVersions []string              `json:"server_versions,omitempty"`
	StartedTs      string                `json:"started_ts,omitempty"`
	// Possible values:
	//   "BOT_DIED"
	//   "CANCELED"
	//   "COMPLETED"
	//   "EXPIRED"
	//   "INVALID"
	//   "KILLED"
	//   "NO_RESOURCE"
	//   "PENDING"
	//   "RUNNING"
	//   "TIMED_OUT"
	State     string   `json:"state,omitempty"`
	Tags      []string `json:"tags,omitempty"`
	TaskId    string   `json:"task_id,omitempty"`
	TryNumber int64    `json:"try_number,omitempty,string"`
	User      string   `json:"user,omitempty"`
}

// SwarmingRpcsStringListPair represents a mapping of string to list of
// strings.
type SwarmingRpcsStringListPair struct {
	Key   string   `json:"key,omitempty"`
	Value []string `json:"value,omitempty"`
}

// SwarmingRpcsFilesRef is a reference to an isolated file on an isolate server.
type SwarmingRpcsFilesRef struct {
	Isolated       string `json:"isolated,omitempty"`
	Isolatedserver string `json:"isolatedserver,omitempty"`
	Namespace      string `json:"namespace,omitempty"`
}
