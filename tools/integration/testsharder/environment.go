// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package testsharder

import (
	"strings"

	"go.fuchsia.dev/fuchsia/tools/build/lib"
)

// Name returns a name calculated from its specfied properties.
func environmentName(env build.Environment) string {
	tokens := []string{}
	addToken := func(s string) {
		if s != "" {
			// s/-/_, so there is no ambiguity among the tokens
			// making up a name.
			s = strings.Replace(s, "-", "_", -1)
			tokens = append(tokens, s)
		}
	}

	addToken(env.Dimensions.DeviceType)
	addToken(env.Dimensions.OS)
	addToken(env.Dimensions.Testbed)
	addToken(env.Dimensions.Pool)
	if env.ServiceAccount != "" {
		addToken(strings.Split(env.ServiceAccount, "@")[0])
	}
	if env.Netboot {
		addToken("netboot")
	}
	return strings.Join(tokens, "-")
}

// resolvesTo gives a partial ordering on DimensionSets in which one resolves to
// anthat if the former's dimensions are given the latter.
func resolvesTo(this, that build.DimensionSet) bool {
	if this.DeviceType != "" && this.DeviceType != that.DeviceType {
		return false
	}
	if this.OS != "" && this.OS != that.OS {
		return false
	}
	if this.Testbed != "" && this.Testbed != that.Testbed {
		return false
	}
	if this.Pool != "" && this.Pool != that.Pool {
		return false
	}
	return true
}
