// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"app/context"
	"syslog/logger"

	"metrics"
)

var (
	initiator     metrics.Initiator
	startTime     time.Time
	sourceVersion string
	targetVersion string
)

func run() (err error) {
	phase := metrics.PhaseEndToEnd

	var queryFreeSpace func() int64

	if blobfs, err := OpenBlobfs(); err == nil {
		defer blobfs.Close()
		queryFreeSpace = func() int64 {
			n, err := blobfs.QueryFreeSpace()
			if err != nil {
				logger.Errorf("error querying blobfs free space! %s", err)
				return -1
			}
			return n
		}
	} else {
		logger.Errorf("error opening blobfs! %s", err)
		queryFreeSpace = func() int64 {
			return -1
		}
	}
	freeSpaceStart := queryFreeSpace()

	history := IncrementOrCreateUpdateHistory(sourceVersion, targetVersion, startTime)
	defer func() {
		if err != nil {
			metrics.Log(metrics.OtaResult{
				//TODO(kevinwells) populate ErrorSource, ErrorCode
				Initiator:      initiator,
				Source:         sourceVersion,
				Target:         targetVersion,
				Attempt:        int64(history.Attempts),
				FreeSpaceStart: freeSpaceStart,
				FreeSpaceEnd:   queryFreeSpace(),
				When:           startTime,
				Duration:       time.Since(startTime),
				Phase:          phase,
				ErrorSource:    "unknown",
				ErrorText:      err.Error(),
				ErrorCode:      -1,
			})
			if err := history.Save(); err != nil {
				logger.Errorf("error writing update history: %s", err)
			}
		}
	}()

	dataPath := filepath.Join("/pkgfs", "packages", "update", "0")

	pFile, err := os.Open(filepath.Join(dataPath, "packages"))
	if err != nil {
		return fmt.Errorf("error opening packages data file! %s", err)
	}
	defer pFile.Close()

	iFile, err := os.Open(filepath.Join("/pkg", "data", "images"))
	if err != nil {
		return fmt.Errorf("error opening images data file! %s", err)
	}
	defer iFile.Close()

	pkgs, imgs, err := ParseRequirements(pFile, iFile)
	if err != nil {
		return fmt.Errorf("could not parse requirements: %s", err)
	}

	resolver, err := ConnectToPackageResolver()
	if err != nil {
		return fmt.Errorf("unable to connect to update service: %s", err)
	}
	defer resolver.Close()

	phase = metrics.PhasePackageDownload
	if err := FetchPackages(pkgs, resolver); err != nil {
		return fmt.Errorf("failed getting packages: %s", err)
	}

	phase = metrics.PhaseImageWrite
	if err := WriteImgs(imgs, dataPath); err != nil {
		return fmt.Errorf("error writing image file: %s", err)
	}

	phase = metrics.PhaseSuccessPendingReboot
	metrics.Log(metrics.OtaResult{
		Initiator:      initiator,
		Source:         sourceVersion,
		Target:         targetVersion,
		Attempt:        int64(history.Attempts),
		FreeSpaceStart: freeSpaceStart,
		FreeSpaceEnd:   queryFreeSpace(),
		When:           startTime,
		Duration:       time.Since(startTime),
		Phase:          phase,
		ErrorSource:    "",
		ErrorText:      "",
		ErrorCode:      0,
	})
	if err := history.Save(); err != nil {
		logger.Errorf("error writing update history: %s", err)
	}

	logger.Infof("system update complete, rebooting...")

	dmctl, err := os.OpenFile("/dev/misc/dmctl", os.O_RDWR, os.ModePerm)
	if err != nil {
		logger.Errorf("error forcing restart: %s", err)
	}
	defer dmctl.Close()
	cmd := []byte("reboot")
	if _, err := dmctl.Write(cmd); err != nil {
		logger.Errorf("error writing to control socket: %s", err)
	}

	return nil
}

type InitiatorValue struct {
	initiator *metrics.Initiator
}

func (i InitiatorValue) String() string {
	if i.initiator == nil {
		return ""
	}
	return i.initiator.String()
}

func (i InitiatorValue) Set(s string) error {
	return i.initiator.Parse(s)
}

func Main() {
	ctx := context.CreateFromStartupInfo()
	if err := logger.InitDefaultLoggerWithTags(ctx.Connector(), "system_updater"); err != nil {
		fmt.Printf("error initializing syslog interface: %s\n", err)
	}

	metrics.Register(ctx)

	flag.Var(&InitiatorValue{&initiator}, "initiator", "what started this update: manual or automatic")
	start := flag.Int64("start", time.Now().UnixNano(), "start time of update attempt, as unix nanosecond timestamp")
	flag.StringVar(&sourceVersion, "source", "", "current OS version")
	flag.StringVar(&targetVersion, "target", "", "target OS version")
	flag.Parse()

	startTime = time.Unix(0, *start)

	if err := run(); err != nil {
		logger.Fatalf("%s", err)
	}
}
