// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package tap_test

import (
	"os"
	"time"

	"go.fuchsia.dev/fuchsia/tools/testing/tap"
	"gopkg.in/yaml.v2"
)

func ExampleProducer_single_test() {
	p := tap.NewProducer(os.Stdout)
	p.Plan(1)
	p.Ok(true, "- this test passed")
	// Output:
	// TAP version 13
	// 1..1
	// ok 1 - this test passed
}

func ExampleProducer_Todo() {
	p := tap.NewProducer(os.Stdout)
	p.Plan(1)
	p.Todo().Ok(true, "implement this test")
	// Output:
	// TAP version 13
	// 1..1
	// ok 1 # TODO implement this test
}

func ExampleProducer_Skip() {
	p := tap.NewProducer(os.Stdout)
	p.Plan(1)
	p.Skip().Ok(true, "implement this test")
	// Output:
	// TAP version 13
	// 1..1
	// ok 1 # SKIP implement this test
}

func ExampleProducer_many_test() {
	p := tap.NewProducer(os.Stdout)
	p.Plan(4)
	p.Ok(true, "- this test passed")
	p.Ok(false, "")
	// Output:
	// TAP version 13
	// 1..4
	// ok 1 - this test passed
	// not ok 2
}

func ExampleProducer_skip_todo_alternating() {
	p := tap.NewProducer(os.Stdout)
	p.Plan(4)
	p.Skip().Ok(true, "implement this test")
	p.Todo().Ok(false, "oh no!")
	p.Skip().Ok(false, "skipped another")
	p.Todo().Skip().Todo().Ok(true, "please don't write code like this")
	// Output:
	// TAP version 13
	// 1..4
	// ok 1 # SKIP implement this test
	// not ok 2 # TODO oh no!
	// not ok 3 # SKIP skipped another
	// ok 4 # TODO please don't write code like this
}

func ExampleProducer_YAML() {
	p := tap.NewProducer(os.Stdout)
	p.Plan(1)
	p.Ok(true, "passed")
	bytes, err := yaml.Marshal(struct {
		Name  string    `yaml:"name"`
		Start time.Time `yaml:"start_time"`
		End   time.Time `yaml:"end_time"`
	}{
		Name:  "foo_test",
		Start: time.Date(2019, 1, 1, 12, 30, 0, 0, time.UTC),
		End:   time.Date(2019, 1, 1, 12, 40, 0, 0, time.UTC),
	})
	if err != nil {
		panic(err)
	}
	p.YAML(bytes)
	// Output:
	// TAP version 13
	// 1..1
	// ok 1 passed
	//  ---
	//  name: foo_test
	//  start_time: 2019-01-01T12:30:00Z
	//  end_time: 2019-01-01T12:40:00Z
	//  ...
}
