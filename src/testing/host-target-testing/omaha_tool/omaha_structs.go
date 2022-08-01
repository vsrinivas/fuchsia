// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package omaha_tool

type requestUpdateCheck struct {
	UpdateDisabled bool `json:"updatedisabled"`
}
type requestApp struct {
	AppId       string             `json:"appid"`
	Cohort      string             `json:"cohort,omitempty"`
	CohortHint  string             `json:"cohorthint,omitempty"`
	CohortName  string             `json:"cohortname,omitempty"`
	UpdateCheck requestUpdateCheck `json:"updatecheck,omitempty"`
	Version     string             `json:"version,omitempty"`
}
type requestConfig struct {
	Protocol string       `json:"protocol"`
	App      []requestApp `json:"app"`
}
type request struct {
	Request requestConfig `json:"request"`
}
type timestamp struct {
	ElapsedSeconds int `json:"elapsed_seconds"`
	ElapsedDays    int `json:"elapsed_days"`
}
type omahaURL struct {
	Codebase string `json:"codebase"`
}
type omahaURLs struct {
	Url []omahaURL `json:"url"`
}
type pkg struct {
	Name     string `json:"name"`
	Fp       string `json:"fp"`
	Required bool   `json:"required"`
}
type packages struct {
	Pkg []pkg `json:"package"`
}
type action struct {
	Run   string `json:"run,omitempty"`
	Event string `json:"event"`
}
type actions struct {
	Action []action `json:"action"`
}
type manifest struct {
	Version  string   `json:"version"`
	Actions  actions  `json:"actions"`
	Packages packages `json:"packages"`
}
type updateCheck struct {
	Status   string    `json:"status"`
	Urls     omahaURLs `json:"urls"`
	Manifest manifest  `json:"manifest"`
}
type app struct {
	CohortHint  string      `json:"cohorthint"`
	AppId       string      `json:"appid"`
	Cohort      string      `json:"cohort"`
	Status      string      `json:"status"`
	CohortName  string      `json:"cohortname"`
	UpdateCheck updateCheck `json:"updatecheck"`
}
type responseConfig struct {
	Server   string    `json:"server"`
	Protocol string    `json:"protocol"`
	DayStart timestamp `json:"timestamp"`
	App      []app     `json:"app"`
}
type response struct {
	Response responseConfig `json:"response"`
}
