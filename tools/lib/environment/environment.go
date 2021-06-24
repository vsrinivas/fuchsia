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
func Ensure() (func(), error) {
	return ensure(osEnv{})
}

func ensure(e environment) (func(), error) {
	needsCleanup := false
	td, ok := e.lookupEnv("TMPDIR")

	cleanupFunc := func() {
		if needsCleanup {
			os.RemoveAll(td)
		}
	}

	if !ok {
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

	tmpEnvVars := []string{"ANDROID_TMP", "HOME", "TEMP", "TEMPDIR", "TMP", "XDG_HOME"}
	for _, env := range tmpEnvVars {
		if _, ok := e.lookupEnv(env); !ok {
			if err := e.setenv(env, td); err != nil {
				cleanupFunc()
				return nil, err
			}
		}
	}
	return cleanupFunc, nil
}
