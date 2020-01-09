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
        "name": "string",
        "config": {},
        "_comment": "string"
      },
      "required": [ "lib" ],
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
        }
      },
      "additionalProperties": false
    },
    "device_routing_profile" : {
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
        "independent_volume_control": "bool"
      },
      "required": [ "device_id", "supported_output_stream_types", "eligible_for_loopback" ],
      "additionalProperties": false
    }
   },
  "type": "object",
  "properties": {
    "volume_curve": {
      "type": "array",
      "items": { "$ref": "#/definitions/volume_mapping" }
    },
    "pipeline": {
      "type": "object",
      "properties": {
        "_comment": "string",
        "name": "string",
        "output_streams": {
          "type": "array",
          "items": { "$ref": "#/definitions/mix_group" }
        },
        "mix": {
          "$ref": "#/definitions/mix_group"
        },
        "linearize": {
          "$ref": "#/definitions/mix_group"
        }
      },
      "additionalProperties": false
    },
    "routing_policy": {
      "type" : "object",
      "properties" : {
        "device_profiles" : {
          "type": "array",
          "items" : { "$ref" : "#/definitions/device_routing_profile" }
        }
      }
    }
  },
  "required": ["volume_curve"],
  "additionalProperties": false
}
