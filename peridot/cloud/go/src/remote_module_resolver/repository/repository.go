// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repository

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"github.com/xeipuuv/gojsonschema"
	"io/ioutil"
	"log"
	"os"
	"path"
)

type ParameterName string
type ParameterType string
type ModuleUrl string
type ActionName string
type FindModulesRequest struct {
	Action ActionName
	// The ability to accept multiple types per named parameter allows the
	// requestor to specify additional polymorphic types.
	Parameters map[ParameterName][]ParameterType
}

type ManifestContent struct {
	Url     ModuleUrl
	Content ParsedManifest
}

type ModuleResolution struct {
	// [0, 1], 1 being most relevant.
	Score    float32
	Manifest ManifestContent
}

type ModuleResolver interface {
	FindModules(req FindModulesRequest) ([]ModuleResolution, error)
	SaveAndIndexManifest(rawModuleManifest []byte) error
	IndexManifest(rawModuleManifest []byte) (ModuleUrl, error)
	UnIndexManifest(moduleUrl ModuleUrl) error
}

type ParameterNameToManifestSet map[ParameterName]ManifestSet
type ParsedManifest map[string]interface{}
type Repository struct {
	// Implements |ModuleResolver|.
	// TODO(vardhan): Use a persistent DB for indexing and searching.

	// The directory where module manifest files are located and where new
	// ones will be stored.
	manifestDir string

	// The following are different indices pointing to a parsed manifest.
	manifestsByModuleUrl  map[ModuleUrl]ParsedManifest
	moduleUrlsByParameter map[ParameterType]ParameterNameToManifestSet
	moduleUrlsByAction    map[ActionName]ManifestSet
}

func NewRepository(manifestDir string) *Repository {
	repo := Repository{
		manifestDir:           manifestDir,
		manifestsByModuleUrl:  make(map[ModuleUrl]ParsedManifest),
		moduleUrlsByParameter: make(map[ParameterType]ParameterNameToManifestSet),
		moduleUrlsByAction:    make(map[ActionName]ManifestSet),
	}
	if manifestFiles, err := ioutil.ReadDir(manifestDir); err == nil {
		for _, file := range manifestFiles {
			if file.IsDir() {
				continue
			}
			if bytes, err := ioutil.ReadFile(path.Join(manifestDir, file.Name())); err == nil {
				_, err := repo.IndexManifest(bytes)
				if err != nil {
					log.Println("Could not index manifest file ", file.Name(), err)
				}
			} else {
				log.Println("Could not read manifest file: ", err)
			}
		}
	} else {
		log.Println("Could not open manifest directory ", manifestDir, ":", err)
	}

	log.Println("Indexed ", len(repo.manifestsByModuleUrl), " manifests")
	return &repo
}

func (r *Repository) SaveAndIndexManifest(rawModuleManifest []byte) error {
	moduleUrl, err := r.IndexManifest(rawModuleManifest)
	if err != nil {
		return err
	}

	if stat, err := os.Stat(r.manifestDir); err != nil || !stat.IsDir() {
		if err := os.Mkdir(r.manifestDir, 0755); err != nil {
			return fmt.Errorf("Could not create manifest directory: %s", err)
		}
		if err := os.Chmod(r.manifestDir, 0755); err != nil {
			return fmt.Errorf("Could not set manifest directory permissions: %s", err)
		}
	}

	shaUrl := sha256.Sum256([]byte(moduleUrl))
	fileName := hex.EncodeToString(shaUrl[:])
	if err = ioutil.WriteFile(path.Join(r.manifestDir, fileName), rawModuleManifest, os.ModePerm); err != nil {
		return fmt.Errorf("Could not save manifest to file: %s", err)
	}

	return nil
}

// On success, returns the module's Url.
func (r *Repository) IndexManifest(rawModuleManifest []byte) (ModuleUrl, error) {
	jsonManifest := make(ParsedManifest)
	err := json.Unmarshal(rawModuleManifest, &jsonManifest)

	schema := gojsonschema.NewStringLoader(ModuleManifestSchema)
	manifest := gojsonschema.NewStringLoader(string(rawModuleManifest))
	result, err := gojsonschema.Validate(schema, manifest)
	if err != nil {
		// The |schema| or |manifest| could not be parsed.
		return "", err
	}
	if !result.Valid() {
		// |manifest| does not follow the module manifest schema.
		return "", errors.New("invalid module manifest schema")
	}

	action := ActionName(jsonManifest["action"].(string))
	moduleUrl := ModuleUrl(jsonManifest["binary"].(string))

	r.UnIndexManifest(moduleUrl)

	// Index the manifest
	r.manifestsByModuleUrl[moduleUrl] = jsonManifest
	if _, ok := r.moduleUrlsByAction[action]; !ok {
		r.moduleUrlsByAction[action] = make(ManifestSet)
	}
	r.moduleUrlsByAction[action][moduleUrl] = struct{}{}

	for _, parameter := range jsonManifest["parameters"].([]interface{}) {
		parameterMap := ParsedManifest(parameter.(map[string]interface{}))
		paramName := ParameterName(parameterMap["name"].(string))
		paramType := ParameterType(parameterMap["type"].(string))

		if _, ok := r.moduleUrlsByParameter[paramType]; !ok {
			r.moduleUrlsByParameter[paramType] = make(ParameterNameToManifestSet)
		}
		if _, ok := r.moduleUrlsByParameter[paramType][paramName]; !ok {
			r.moduleUrlsByParameter[paramType][paramName] = make(ManifestSet)
		}
		r.moduleUrlsByParameter[paramType][paramName][moduleUrl] = struct{}{}
	}
	return moduleUrl, nil
}

func (r *Repository) UnIndexManifest(moduleUrl ModuleUrl) error {
	if jsonManifest, ok := r.manifestsByModuleUrl[moduleUrl]; ok {
		if parameters, ok := jsonManifest["parameters"]; ok {
			for _, parameter := range parameters.([]interface{}) {
				paramName := ParameterName(ParsedManifest(parameter.(map[string]interface{}))["name"].(string))
				paramType := ParameterType(ParsedManifest(parameter.(map[string]interface{}))["type"].(string))
				delete(r.moduleUrlsByParameter[paramType][paramName], moduleUrl)
				if len(r.moduleUrlsByParameter[paramType][paramName]) == 0 {
					delete(r.moduleUrlsByParameter[paramType], paramName)
				}
				if len(r.moduleUrlsByParameter[paramType]) == 0 {
					delete(r.moduleUrlsByParameter, paramType)
				}
			}
		}

		delete(r.moduleUrlsByAction, jsonManifest["action"].(ActionName))
		delete(r.manifestsByModuleUrl, moduleUrl)
		return nil
	}
	return fmt.Errorf("could not remove manifest: none exists for %s", moduleUrl)
}

func (r *Repository) findManifestsByParameter(paramName ParameterName, paramType ParameterType) ManifestSet {
	if nameMap, ok := r.moduleUrlsByParameter[paramType]; ok {
		if manifests, ok := nameMap[paramName]; ok {
			return manifests
		}
	}
	return nil
}

func hasAllRequiredParameters(req FindModulesRequest, moduleManifest ParsedManifest) bool {
	if parameters, ok := moduleManifest["parameters"]; ok {
		for _, constraint := range parameters.([]interface{}) {
			constraintKeys := constraint.(map[string]interface{})
			paramName := ParameterName(constraintKeys["name"].(string))
			// Parameters are required by default. Check if this was marked optional.
			if isRequired, found := constraintKeys["required"]; !found || (found && isRequired.(bool)) {
				// It is required. Check that it was specified in the request
				if _, found := req.Parameters[paramName]; !found {
					return false
				}
			}
		}
	}
	return true
}

// TODO(vardhan): Make this a separate exported API, and rename |FindModules| ->
// |FindModulesByAction|
func (r *Repository) findModulesByParamTypes(req FindModulesRequest) ([]ModuleResolution, error) {
	return []ModuleResolution{}, nil
}

func (r *Repository) FindModules(req FindModulesRequest) ([]ModuleResolution, error) {
	if req.Action == "" {
		return r.findModulesByParamTypes(req)
	}

	// 1. Find all module manifests that contain the action in the request.
	candidateModules := make(ManifestSet)
	for moduleUrl := range r.moduleUrlsByAction[req.Action] {
		candidateModules[moduleUrl] = struct{}{}
	}

	// 2. Find all module manifests that contain the parameters in request.
	// 3. Intersect the 1) and 2) to get the possible candidate manifests.
	for paramName, paramTypes := range req.Parameters {
		manifests := make(ManifestSet)
		for _, paramType := range paramTypes {
			manifests.merge(r.findManifestsByParameter(paramName, paramType))
		}
		candidateModules.intersect(manifests)
	}

	// 4. Filter through the candidates to make sure their required parameters are
	//    provided in the request.
	results := []ModuleResolution{}
	for candidateModuleUrl := range candidateModules {
		if !hasAllRequiredParameters(req, r.manifestsByModuleUrl[candidateModuleUrl]) {
			continue
		}

		results = append(results, ModuleResolution{
			Manifest: ManifestContent{
				Url:     candidateModuleUrl,
				Content: r.manifestsByModuleUrl[candidateModuleUrl]},
			Score: 1.0})
	}
	return results, nil
}
