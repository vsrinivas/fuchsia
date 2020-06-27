// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package templates

const TemplateJson = `{
  "licenses": {
    "unused": {
			"categories": [{{ range $_, $license := .Unused }}
				"{{ (getCategory $license) }}",{{ end }}
			],
		},
    "used": {
			"license": [
				{{ range $_, $license := .Used }}{
				"category": "{{ (getCategory $license) }}",
				"files": [{{ range $file := (getFiles $license) }}
					"{{ $file }}",{{ end }}
				],
				"text": "{{ (getEscapedText $license) }}",
				},{{ end }}
      ]
    }
	},
	"files": {
		"unlicensed": []
	}
}
`
