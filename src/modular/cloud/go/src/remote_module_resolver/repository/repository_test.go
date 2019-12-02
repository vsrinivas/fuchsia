// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repository

import (
	"reflect"
	"testing"
)

var moduleManifestsRepo = map[ModuleUrl][]byte{
	"module_url1": []byte(`
		{
			"binary": "module_url1",
			"action": "com.fuchsia.action",
			"parameters": [
					{
							"name": "ParamName",
							"type": "ParamType"
					}
			]
		}`),

	"module_url2": []byte(`
		{
			"binary": "module_url2",
			"action": "com.fuchsia.action",
			"parameters": [
					{
							"name": "ParamName",
							"type": "ParamType"
					}
			]
		}`),

	"module_url3": []byte(`
		{
			"binary": "module_url3",
			"action": "com.fuchsia.action",
			"parameters": [
					{
							"name": "ParamName",
							"type": "DifferentParamType"
					}
			]
		}`),
}

// |FindModulesTestCase| describes a single test case that involves issuing a
// FindModules query against a repository of modules, with an expected set of
// modules as the result.
type FindModulesTestCase struct {
	// Which modules to index from |moduleManifestRepo| for this particular test?
	IndexedModules []ModuleUrl
	Query          FindModulesRequest
	// Which expected set of modules should return from the |Query| above? This
	// list is treated like a set.
	ExpectedResults []ModuleUrl
}

func (testCase FindModulesTestCase) run(t *testing.T) {
	var resolver ModuleResolver = NewRepository("")
	for _, moduleUrl := range testCase.IndexedModules {
		_, err := resolver.IndexManifest(moduleManifestsRepo[moduleUrl])
		if err != nil {
			t.Errorf("FAIL: Could not index module manifest '%s': %s", moduleUrl, err)
			return
		}
	}
	results, err := resolver.FindModules(testCase.Query)
	if err != nil {
		t.Errorf("FAIL: Could not execute query: %s", err)
		return
	}

	resultUrls := []ModuleUrl{}
	for _, result := range results {
		resultUrls = append(resultUrls, result.Manifest.Url)
	}

	if !reflect.DeepEqual(listToManifestSet(resultUrls), listToManifestSet(testCase.ExpectedResults)) {
		t.Errorf("FAIL: Did not get expected results. #expected = %d, #actual = %d",
			len(testCase.ExpectedResults),
			len(results))
	}
}

func TestExactMatchSingleModule(t *testing.T) {
	FindModulesTestCase{
		IndexedModules: []ModuleUrl{"module_url1"},
		Query: FindModulesRequest{
			Action: "com.fuchsia.action",
			Parameters: map[ParameterName][]ParameterType{
				"ParamName": []ParameterType{"ParamType"},
			},
		},
		ExpectedResults: []ModuleUrl{"module_url1"},
	}.run(t)
}

func TestExactMatchMultipleModules(t *testing.T) {
	FindModulesTestCase{
		IndexedModules: []ModuleUrl{"module_url1", "module_url2"},
		Query: FindModulesRequest{
			Action: "com.fuchsia.action",
			Parameters: map[ParameterName][]ParameterType{
				"ParamName": []ParameterType{"ParamType", "UnknownParamType"},
			},
		},
		ExpectedResults: []ModuleUrl{"module_url1", "module_url2"},
	}.run(t)
}

func TestNoMatchUnknownParamType(t *testing.T) {
	FindModulesTestCase{
		IndexedModules: []ModuleUrl{"module_url1"},
		Query: FindModulesRequest{
			Action: "com.fuchsia.action",
			Parameters: map[ParameterName][]ParameterType{
				"ParamName": []ParameterType{"UnknownParamType"},
			},
		},
		ExpectedResults: []ModuleUrl{},
	}.run(t)
}

func TestDifferentParamTypes(t *testing.T) {
	FindModulesTestCase{
		IndexedModules: []ModuleUrl{"module_url1", "module_url3"},
		Query: FindModulesRequest{
			Action: "com.fuchsia.action",
			Parameters: map[ParameterName][]ParameterType{
				"ParamName": []ParameterType{"ParamType"},
			},
		},
		ExpectedResults: []ModuleUrl{"module_url1"},
	}.run(t)
}
