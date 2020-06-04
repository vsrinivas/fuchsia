package lib

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
)

type Config struct {
	FilesRegex          []string        `json:"filesRegex,omitempty"`
	SkipDirsRegex       []string        `json:"skipDirsRegex"`
	SkipFilesRegex      []string        `json:"skipFilesRegex"`
	TextExtensions      map[string]bool `json:"textExtensions"`
	MaxReadSize         int             `json:"maxReadSize"`
	SeparatorWidth      int             `json:"separatorWidth"`
	OutputFilePrefix    string          `json:"outputFilePrefix"`
	OutputFileExtension string          `json:"outputFileExtension"`
	Product             string          `json:"product"`
	LicenseFilesInTree  []string        `json:"licenseFilesInTree"`
	LicensePatternDir   string          `json:"licensePatternDir"`
	BaseDir             string          `json:"BaseDir"`
}

func (config *Config) Init(configJson *string) {
	jsonFile, err := os.Open(*configJson)
	if err != nil {
		panic(err)
	}
	defer jsonFile.Close()
	byteValue, _ := ioutil.ReadAll(jsonFile)
	err = json.Unmarshal(byteValue, &config)
	if err != nil {
		fmt.Println(err)
		panic("error: config")
	}
}
