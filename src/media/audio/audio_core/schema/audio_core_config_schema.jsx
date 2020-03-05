{
  "definitions": {
    "volume_mapping": {
      "type": "object",
      "properties": {
        "level" : "number",
        "db": "number"
      },
      "required": ["level", "db"],
      "additionalProperties": false
    },
    "output_stream_type": {
      "enum": [
        "background", "communications", "interruption", "media", "system_agent"
      ]
    },
    "effect": {
      "type": "object",
      "properties": {
        "lib": "string",
        "effect": "string",
        "name": "string",
        "config": {},
        "_comment": "string"
      },
      "required": [ "lib", "effect" ],
      "additionalProperties": false
    },
    "mix_group": {
      "type": "object",
      "properties": {
        "name": "string",
        "_comment": "string",
        "streams": {
          "type": "array",
          "items": { "$ref": "#/definitions/output_stream_type" }
        },
        "effects": {
          "type": "array",
          "items": { "$ref": "#/definitions/effect" }
        },
        "inputs": {
          "type": "array",
          "items": { "$ref": "#/definitions/mix_group" }
        },

        // If |true|, then the output from this mix group (including applied effects) will be the
        // stream used as the loopback for the pipeline.
        //
        // Only a single mix group in a pipeline may set this to true; defaults to false if
        // unspecified.
        "loopback": "bool",
        // The output rate of this stage. For the root mix group, this will be the target rate that
        // will be requested from hardware. A different rate may be chosen if the hardware does not
        // support the rate requested.
        "output_rate": "integer"
      },
      "additionalProperties": false
    },
    "output_device_profile" : {
      "type": "object",
      "properties" : {
        "device_id": {
          "type" : "string"
        },
        "supported_output_stream_types": {
          "type": "array",
          "items" : { "$ref" : "#definitions/output_stream_type" }
        },

        // Whether this device is eligible to be looped back to loopback capturers.
        "eligible_for_loopback": "bool",

        // Whether this device has independent volume control, and should therefore
        // receive routed streams at unity gain.
        "independent_volume_control": "bool",

        // The mix pipeline to construct for this device.
        "pipeline": { "$ref" : "#definitions/mix_group" }
      },
      "required": [ "device_id", "supported_output_stream_types", "eligible_for_loopback" ],
      "additionalProperties": false
    },
    "thermal_policy_entry" : {
      "type": "object",
      "properties" : {
        "target_name": "string",
        "_comment": "string",
        "states": {
          "type" : "array",
          "items" : {
            "type" : "object",
            "properties": {
              "trip_point" : {
                "type": "integer",
                "minimum": 1,
                "maximum": 100
              },
              "_comment": "string",
              "config" : {}
            },
            "required": [ "trip_point", "config" ]
          }
        }
      },
      "required": [ "target_name", "states" ]
    },
    "input_device_profile" : {
      "type": "object",
      "properties" : {
        "device_id": {
          "type" : "string"
        },
        // The target rate for this device. A different rate may be chosen if the driver does
        // not support the rate requested.
        "rate": "integer"
      },
      "required": [ "device_id", "rate" ],
      "additionalProperties": false
    }
  },
  "type": "object",
  "properties": {
    "volume_curve": {
      "type": "array",
      "items": { "$ref": "#/definitions/volume_mapping" }
    },
    "output_devices" : {
      "type": "array",
      "items" : { "$ref" : "#/definitions/output_device_profile" }
    },
    "input_devices" : {
      "type": "array",
      "items" : { "$ref" : "#/definitions/input_device_profile" }
    },
    "thermal_policy": {
      "type" : "array",
      "items": { "$ref" : "#/definitions/thermal_policy_entry" }
    }
  },
  "required": ["volume_curve"],
  "additionalProperties": false
}
