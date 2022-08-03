// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// Package environment ensures correct environment is available.
package environment

import (
	"io/ioutil"
	"os"
)

type environment interface {
	getenv(k string) string
	lookupEnv(k string) (string, bool)
	setenv(k, v string) error
}

func (osEnv) getenv(k string) string {
	return os.Getenv(k)
}

func (osEnv) lookupEnv(k string) (string, bool) {
	return os.LookupEnv(k)
}

func (osEnv) setenv(k, v string) error {
	return os.Setenv(k, v)
}

type osEnv struct{}

// Ensure ensures that all expected environment variables are set.
//
// This includes various TMP variable variations.
// If isolated=true, this will set all TMP variables to a new temp directory.
func Ensure(isolated bool) (func(), error) {
	return ensure(osEnv{}, isolated)
}

// TempDirEnvVars returns all the environment variable keys that will be set to
// point to $TMPDIR by this library. In addition to $TMP and the like, we
// override $HOME and related variables to discourage tools from writing to the
// global home directory and persisting data between tasks.
//
// Exposed as a function rather than a variable to ensure it's not possible to
// mutate a global list.
func TempDirEnvVars() []string {
	return []string{
		"ANDROID_TMP",
		"HOME",
		"TEMP",
		"TEMPDIR",
		"TMP",
		"XDG_CACHE_HOME",
		"XDG_CONFIG_HOME",
		"XDG_DATA_HOME",
		"XDG_HOME",
		"XDG_STATE_HOME",
	}
}

func ensure(e environment, isolated bool) (func(), error) {
	needsCleanup := false
	td, ok := e.lookupEnv("TMPDIR")

	cleanupFunc := func() {
		if needsCleanup {
			os.RemoveAll(td)
		}
	}

	if !ok || isolated {
		tmp, err := ioutil.TempDir("", "wt")
		if err != nil {
			return nil, err
		}

		td = tmp
		needsCleanup = true

		if err := e.setenv("TMPDIR", td); err != nil {
			cleanupFunc()
			return nil, err
		}
	}

	for _, env := range TempDirEnvVars() {
		if _, ok := e.lookupEnv(env); !ok || isolated {
			if err := e.setenv(env, td); err != nil {
				cleanupFunc()
				return nil, err
			}
		}
	}
	return cleanupFunc, nil
}
