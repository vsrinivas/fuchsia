// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"remote_module_resolver/repository"
)

var (
	usage       = "Starts the module resolver HTTP service."
	manifestDir = flag.String("manifests-directory", "/tmp/module_manifests", "Path to the directory containing module manifests.")
	httpPort    = flag.Int("port", 15321, "TCP Port to run the resolver HTTP server on.")
)

func main() {
	flag.CommandLine.Usage = func() {
		fmt.Println(usage)
		flag.CommandLine.PrintDefaults()
	}
	flag.Parse()
	log.SetPrefix("remote module resolver: ")
	log.SetFlags(log.Ltime & log.LstdFlags)
	log.Println("Resolver listening to port ", *httpPort, " with manifest directory ", *manifestDir)

	repo := repository.NewRepository(*manifestDir)
	// /addmodule
	//    POST body:  JSON module manifest contents
	http.HandleFunc("/addmodule", func(writer http.ResponseWriter,
		request *http.Request) {
		if request.Method != "POST" {
			writer.WriteHeader(http.StatusBadRequest)
			writer.Write(nil)
			return
		}

		defer request.Body.Close()
		bytes, err := ioutil.ReadAll(request.Body)
		if err != nil {
			log.Println(err)
			writer.WriteHeader(http.StatusBadRequest)
			return
		}
		log.Println("/addmodule: ", string(bytes))
		if err = repo.SaveAndIndexManifest(bytes); err != nil {
			log.Println(err)
			writer.WriteHeader(http.StatusBadRequest)
			return
		}
		writer.WriteHeader(http.StatusOK)
	})

	// /findmodules
	//   POST request body: json-encoded |repository.FindModulesRequest|
	//   POST response body:  json-encoded |repository.ModuleResolution|
	http.HandleFunc("/findmodules", func(writer http.ResponseWriter,
		request *http.Request) {
		if request.Method != "POST" {
			writer.WriteHeader(http.StatusBadRequest)
			return
		}

		defer request.Body.Close()
		bytes, err := ioutil.ReadAll(request.Body)
		if err != nil {
			writer.WriteHeader(http.StatusBadRequest)
			log.Println(err)
			return
		}

		log.Println("/findmodules: ", string(bytes))
		var reqInJson map[string]interface{}
		if err = json.Unmarshal(bytes, &reqInJson); err != nil {
			writer.WriteHeader(http.StatusBadRequest)
			log.Println(err)
			return
		}

		paramsMap := make(map[repository.ParameterName][]repository.ParameterType)
		for _, kv := range reqInJson["parameters"].([]interface{}) {
			kv := kv.(map[string]interface{})
			paramsMap[kv["@k"].(repository.ParameterName)] = kv["@v"].([]repository.ParameterType)
		}
		results, err := repo.FindModules(repository.FindModulesRequest{Action: repository.ActionName(reqInJson["action"].(string)), Parameters: paramsMap})
		if err != nil {
			writer.WriteHeader(http.StatusBadRequest)
			log.Println(err)
			return
		}

		encoded, err := json.Marshal(results)
		if err != nil {
			writer.WriteHeader(http.StatusBadRequest)
			log.Println(err)
			return
		}
		writer.WriteHeader(http.StatusOK)
		writer.Write(encoded)
		return
	})

	log.Fatal(http.ListenAndServe(fmt.Sprintf(":%d", *httpPort), nil))
}
