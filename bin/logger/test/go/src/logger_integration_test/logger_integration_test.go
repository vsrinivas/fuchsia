// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"app/context"
	"bytes"
	"crypto/rand"
	"fmt"
	"log"
	"math/big"
	"os"
	"os/exec"
	"strings"
	"syslog/logger"
	"testing"
	"time"
)

func TestFullStack(t *testing.T) {
	loglistener := "/system/bin/log_listener"
	if _, err := os.Stat(loglistener); err != nil {
		t.Fatalf("error stating log listener: %s", err)
	}
	var max big.Int
	max.SetInt64(10000)
	r, err := rand.Int(rand.Reader, &max)
	if err != nil {
		log.Fatal(err)
	}
	tag := fmt.Sprintf("logger_test_%s", r)
	ctx := context.CreateFromStartupInfo()
	if err := logger.InitDefaultLoggerWithTags(ctx.GetConnector(), tag); err != nil {
		t.Fatal(err)
	}
	if err := logger.Infof("integer: %d", 10); err != nil {
		t.Fatal(err)
	}
	cmd := exec.Command(loglistener, "--tag", tag)
	var out bytes.Buffer
	cmd.Stdout = &out
	err = cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	defer cmd.Process.Kill()

	step_sleep := 10    // in ms
	total_sleep := 5000 // in ms
	tries := total_sleep / step_sleep
	for i := 0; i < tries; i++ {
		if out.Len() == 0 {
			time.Sleep(time.Duration(step_sleep) * time.Millisecond)
		} else {
			break
		}
	}
	expected := fmt.Sprintf("[0][%s] INFO: integer: 10\n", tag)
	if !strings.HasSuffix(out.String(), expected) {
		t.Fatalf("expected suffix: %q, got: %q", expected, out.String())
	}
}
