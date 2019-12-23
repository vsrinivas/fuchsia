// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package system_updater

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"syscall/zx"
	"syscall/zx/fdio"
	"syscall/zx/fidl"
	"time"

	"app/context"
	devmgr "fidl/fuchsia/device/manager"
	"syslog"

	"metrics"
)

var (
	initiator     metrics.Initiator
	startTime     time.Time
	sourceVersion string
	targetVersion string
	updateURL     string
	reboot        bool
)

func run() (err error) {
	metrics.Log(metrics.OtaStart{
		Initiator: initiator,
		Target:    targetVersion,
		When:      startTime,
	})
	phase := metrics.PhaseEndToEnd

	var queryFreeSpace func() int64

	if blobfs, err := OpenBlobfs(); err == nil {
		defer blobfs.Close()
		queryFreeSpace = func() int64 {
			n, err := blobfs.QueryFreeSpace()
			if err != nil {
				syslog.Errorf("error querying blobfs free space! %s", err)
				return -1
			}
			return n
		}
	} else {
		syslog.Errorf("error opening blobfs! %s", err)
		queryFreeSpace = func() int64 {
			return -1
		}
	}
	freeSpaceStart := queryFreeSpace()

	history := IncrementOrCreateUpdateHistory(sourceVersion, targetVersion, startTime)
	defer func() {
		if err != nil {
			metrics.Log(metrics.OtaResultAttempt{
				Initiator: initiator,
				Target:    targetVersion,
				Attempt:   int64(history.Attempts),
				Phase:     phase,
				Status:    metrics.StatusFromError(err),
			})
			metrics.Log(metrics.OtaResultDuration{
				Initiator: initiator,
				Target:    targetVersion,
				Duration:  time.Since(startTime),
				Phase:     phase,
				Status:    metrics.StatusFromError(err),
			})
			metrics.Log(metrics.OtaResultFreeSpaceDelta{
				Initiator:      initiator,
				Target:         targetVersion,
				FreeSpaceDelta: queryFreeSpace() - freeSpaceStart,
				Duration:       time.Since(startTime),
				Phase:          phase,
				Status:         metrics.StatusFromError(err),
			})
			if err := history.Save(); err != nil {
				syslog.Errorf("error writing update history: %s", err)
			}
		}
	}()

	resolver, err := ConnectToPackageResolver()
	if err != nil {
		return fmt.Errorf("unable to connect to update service: %s", err)
	}
	defer resolver.Close()

	paver, err := ConnectToPaver()
	if err != nil {
		return fmt.Errorf("unable to connect to paver service: %s", err)
	}
	defer paver.Close()

	dataPath, err := CacheUpdatePackage(updateURL, resolver)
	if err != nil {
		return fmt.Errorf("error caching update package! %s", err)
	}

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

	phase = metrics.PhasePackageDownload
	if err := FetchPackages(pkgs, resolver); err != nil {
		return fmt.Errorf("failed getting packages: %s", err)
	}

	if err := ValidateImgs(imgs, dataPath); err != nil {
		return fmt.Errorf("failed to validate imgs: %s", err)
	}

	phase = metrics.PhaseImageWrite
	if err := WriteImgs(paver, imgs, dataPath); err != nil {
		return fmt.Errorf("error writing image file: %s", err)
	}

	phase = metrics.PhaseSuccessPendingReboot
	metrics.Log(metrics.OtaResultAttempt{
		Initiator: initiator,
		Target:    targetVersion,
		Attempt:   int64(history.Attempts),
		Phase:     phase,
		Status:    metrics.StatusFromError(nil),
	})
	metrics.Log(metrics.OtaResultDuration{
		Initiator: initiator,
		Target:    targetVersion,
		Duration:  time.Since(startTime),
		Phase:     phase,
		Status:    metrics.StatusFromError(nil),
	})
	metrics.Log(metrics.OtaResultFreeSpaceDelta{
		Initiator:      initiator,
		Target:         targetVersion,
		FreeSpaceDelta: queryFreeSpace() - freeSpaceStart,
		Duration:       time.Since(startTime),
		Phase:          phase,
		Status:         metrics.StatusFromError(nil),
	})

	if err := UpdateCurrentChannel(); err != nil {
		syslog.Errorf("%v", err)
	}

	if err := history.Save(); err != nil {
		syslog.Errorf("error writing update history: %s", err)
	}

	if reboot {
		syslog.Infof("system update complete, rebooting...")
		SendReboot()
	} else {
		syslog.Infof("system update complete, new version will run on next boot.")
	}

	return nil
}

func SendReboot() {
	channel_local, channel_remote, err := zx.NewChannel(0)
	if err != nil {
		syslog.Errorf("error creating channel: %s", err)
		return
	}

	err = fdio.ServiceConnect(
		"/svc/fuchsia.device.manager.Administrator", zx.Handle(channel_remote))
	if err != nil {
		syslog.Errorf("error connecting to devmgr service: %s", err)
		return
	}

	var administrator = devmgr.AdministratorInterface(
		fidl.ChannelProxy{Channel: zx.Channel(channel_local)})
	var status int32
	status, err = administrator.Suspend(devmgr.SuspendFlagReboot)
	if err != nil || status != 0 {
		syslog.Errorf("error sending restart to Administrator: %s status: %d", err, status)
	}
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

	{
		if l, err := syslog.NewLoggerWithDefaults(ctx.Connector(), "system_updater"); err != nil {
			fmt.Println(err)
		} else {
			syslog.SetDefaultLogger(l)
		}
	}

	metrics.Register(ctx)

	flag.Var(&InitiatorValue{&initiator}, "initiator", "what started this update: manual or automatic")
	start := flag.Int64("start", time.Now().UnixNano(), "start time of update attempt, as unix nanosecond timestamp")
	flag.StringVar(&sourceVersion, "source", "", "current OS version")
	flag.StringVar(&targetVersion, "target", "", "target OS version")
	flag.StringVar(&updateURL, "update", "fuchsia-pkg://fuchsia.com/update", "update package URL")
	flag.BoolVar(&reboot, "reboot", true, "if true, reboot the system after successful OTA")
	flag.Parse()

	startTime = time.Unix(0, *start)

	if err := run(); err != nil {
		syslog.Fatalf("%s", err)
	}
}
