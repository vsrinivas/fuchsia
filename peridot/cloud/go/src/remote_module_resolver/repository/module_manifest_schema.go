// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package repository

// TODO(vardhan): Use a flag to pass in path-to-manifest-schema that overrides
// this default schema.
const ModuleManifestSchema = `{
  "$schema": "http://json-schema.org/schema#",
  "title": "Schema for 'action_template' metadata file",
  "definitions": {
    "parameterArray": {
      "type": "array",
      "items": {
        "$ref": "#/definitions/parameter"
      },
      "additionalItems": false,
      "uniqueItems": true,
      "minItems": 1
    },
    "parameter": {
      "type": "object",
      "properties": {
        "name": {
          "$ref": "#/definitions/alphaNumString"
        },
        "type": {
          "type": "string"
        },
        "required": {
          "type": "boolean"
        }
      },
      "required": [
        "name",
        "type"
      ],
      "additionalProperties": false
    },
    "alphaNumString": {
      "type": "string",
      "pattern": "^[a-zA-Z0-9_]+$"
    },
    "compositionPattern": {
      "type": "string",
      "enum": [
        "ticker",
        "comments-right"
      ]
    }
  },
  "type": "object",
  "properties": {
    "binary": {
      "type": "string"
    },
    "suggestion_headline": {
      "type": "string"
    },
    "action": {
      "type": "string"
    },
    "parameters": {
      "$ref": "#/definitions/parameterArray"
    },
    "composition_pattern": {
      "$ref": "#/definitions/compositionPattern"
    }
  },
  "required": [
    "binary",
    "action"
  ],
  "additionalProperties": false
}`
