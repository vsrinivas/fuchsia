// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_SCHEMA_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_SCHEMA_H_

namespace forensics {
namespace feedback_data {

constexpr const char* kMetadataSchema = R"({
   "type":"object",
   "snapshot_version": {
       "type": "string"
   },
   "metadata_version": {
       "type": "string"
   },
   "files": {
       "type": "object",
       "patternProperties":{
          "^.*$":{
             "type":"object",
             "properties":{
                "utc_monotonic_difference": {
                  "type": "object",
                  "properties": {
                      "value": {
                        "type": "integer"
                      },
                      "units": {
                        "type": "string"
                      },
                      "required":[
                        "value",
                        "units"
                      ]
                  }
                },
                "state":{
                   "type":"string",
                   "enum":[
                      "complete",
                      "partial",
                      "missing"
                   ]
                },
                "error":{
                   "type":"string"
                }
             },
             "required":[
                "state"
             ]
          }
       },
       "properties":{
          "annotations.json":{
             "type":"object",
             "properties":{
                "utc_monotonic_difference": {
                  "type": "integer"
                },
                "state":{
                   "type":"string",
                   "enum":[
                      "complete",
                      "partial",
                      "missing"
                   ]
                },
                "missing annotations":{
                   "type":"object",
                   "patternProperties":{
                      "^.*$":{
                         "type":"string"
                      }
                   }
                },
                "present annotations":{
                   "type":"array",
                   "items":{
                      "type":"string"
                   }
                }
             },
             "required":[
                "state",
                "missing annotations",
                "present annotations"
             ]
          }
       }
    },
    "required": [
      "snapshot_version",
      "metadata_version",
      "files"
    ]
})";

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_SCHEMA_H_
