// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

// Program to map Fuchsia devices to their associated serial connection on a
// controller machine.  Connects to all available serial connections on the
// controller machine, invokes `dlog` (kernel debug log) and attempts to
// extract nodename=(.*?) from netsvc start up.

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"path"
	"regexp"
	"time"

	"fuchsia.googlesource.com/tools/logger"
	"fuchsia.googlesource.com/tools/serial"
)

const (
	// The default path under which /lib/udev/rules.d/60-serial.rules
	// will create serial device files with full identifiers.
	serialDir = "/dev/serial/by-id"
)

func main() {
	ctx := context.Background()

	files, err := ioutil.ReadDir(serialDir)
	if err != nil {
		logger.Fatalf(ctx, "failed to readdir() %s: %s", serialDir, err)
	}

	mapping := make(map[string]string)
	re := regexp.MustCompile("nodename='(.*?)'")

	for _, f := range files {
		fPath := path.Join(serialDir, f.Name())

		device, err := serial.Open(fPath)
		if err != nil {
			logger.Errorf(ctx, "unable to open() %s: %s", fPath, err)
		}

		_, err = io.WriteString(device, "\ndlog\n")
		if err != nil {
			logger.Errorf(ctx, "failed to writestring() to %s: %s", fPath, err)
		}

		errs := make(chan error)
		c := make(chan string)

		go func() {
			defer close(c)
			defer close(errs)

			scanner := bufio.NewScanner(device)
			for scanner.Scan() {
				line := scanner.Text()
				match := re.FindStringSubmatch(line)
				if match != nil {
					c <- match[1]
					break
				}
			}
			if err := scanner.Err(); err != nil {
				errs <- fmt.Errorf("scan() error: %s", err)
			}
		}()

		select {
		case err := <-errs:
			logger.Errorf(ctx, "%s", err)
		// Timeout after 10 seconds.
		case <-time.After(10 * time.Second):
			logger.Errorf(ctx, "timed out %s", fPath)
		case nodename := <-c:
			mapping[nodename] = fPath
		}

		device.Close()
	}

	if len(mapping) > 0 {
		fmt.Println("device:serial mapping:")
		for k, v := range mapping {
			fmt.Printf("%s\t%s\n", k, v)
		}
	} else {
		fmt.Println("found nothing!")
	}
}
