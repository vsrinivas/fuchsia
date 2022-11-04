{
  "definitions": {
    "channel_count": {
      "type": "integer",
      "minimum": 1,
      "maximum": 8
    },
    "device_id": {
      "type": "string",
      "oneOf": [
        {
          "pattern": "^[A-Fa-f0-9]{32}$"
        }, {
          "enum": ["*"]
        }
      ]
    },
    "device_id_list": {
      "type": "array",
      "items": { "$ref": "#/definitions/device_id" }
    },
    "volume_mapping": {
      "type": "object",
      "properties": {
        "level" : "number",
        "db": "number"
      },
      "required": ["level", "db"],
      "additionalProperties": false
    },
    "stream_type": {
      "type": "string",
      "enum": [
        "background",
        "communications",
        "interruption",
        "media",
        "system_agent",
        "render:background",
        "render:communications",
        "render:interruption",
        "render:media",
        "render:system_agent",
        "render:ultrasound",
        "capture:foreground",
        "capture:background",
        "capture:system_agent",
        "capture:communications",
        "capture:ultrasound",
        "capture:loopback"
      ]
    },
    "effect_v1": {
      "type": "object",
      "properties": {
        "lib": "string",
        "effect": "string",
        // A unique name to identify an effect instance; necessary for enabling effect updates.
        "name": "string",
        // The number of channels in the audio stream output by this effect. If unspecified this
        // will be equal to the number of input channels.
        "output_channels": { "$ref" : "#/definitions/channel_count" },
        "config": {},
        "_comment": "string"
      },
      "required": [ "lib", "effect", "name" ],
      "additionalProperties": false
    },
    "effect_v2": {
      "type": "object",
      "properties": {
        "name": "string",
        "_comment": "string"
      },
      "required": [ "name" ],
      "additionalProperties": false
    },
    "mix_group": {
      "type": "object",
      "properties": {
        "name": "string",
        "_comment": "string",
        "streams": {
          "type": "array",
          "items": { "$ref": "#/definitions/stream_type" }
        },
        "effects": {
          "type": "array",
          "items": { "$ref": "#/definitions/effect_v1" }
        },
        "effect_over_fidl": { "$ref": "#/definitions/effect_v2" },
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
        "output_rate": "integer",

        // The output rate of this stage. This is the channelization that the sampler will produce
        // _before_ applying any effects, which could apply further rechannelizations.
        //
        // This will default to '2' if unspecified.
        "output_channels": { "$ref" : "#/definitions/channel_count" },

        // Gain limits for this stage.
        "min_gain_db": "number",  // defaults to fuchsia::media::audio::MUTED_GAIN_DB if unspecified
        "max_gain_db": "number"   // defaults to fuchsia::media::audio::MAX_GAIN_DB if unspecified
      },
      "additionalProperties": false
    },
    "output_device_profile" : {
      "type": "object",
      "properties" : {
        "device_id": {
          "oneOf": [
            {"$ref": "#/definitions/device_id"},
            {"$ref": "#/definitions/device_id_list"}
          ]
        },
        "supported_output_stream_types": {
          "type": "array",
          "items": { "$ref": "#/definitions/stream_type" }
        },
        "supported_stream_types": {
          "type": "array",
          "items": { "$ref": "#/definitions/stream_type" }
        },

        // Whether this device is eligible to be looped back to loopback capturers.
        "eligible_for_loopback": "bool",

        // Whether this device has independent volume control, and should therefore
        // receive routed streams at unity gain.
        "independent_volume_control": "bool",

        // Gain value (in decibels) applied to device driver upon initialization.
        // If the key is not specified, a default gain value of 0.0 will be used.
        "driver_gain_db": "number",

        // The mix pipeline to construct for this device.
        "pipeline": { "$ref" : "#/definitions/mix_group" }
      },
      "required": [ "device_id" ],
      "oneOf": [
        {
          "required": [ "supported_output_stream_types" ]
        },
        {
          "required": [ "supported_stream_types" ]
        }
      ],
      "additionalProperties": false
    },
    "thermal_state_format": {
      "type": "object",
      "properties": {
        "state_number": {
          "type": "integer",
          "minimum": 0
        },
        "effect_configs": {
          "type": "object"
        }
      },
      "required": [ "state_number", "effect_configs" ],
      "additionalProperties": false
    },
    "input_device_profile" : {
      "type": "object",
      "properties" : {
        "device_id": {
          "oneOf": [
            {"$ref": "#/definitions/device_id"},
            {"$ref": "#/definitions/device_id_list"}
          ]
        },

        "supported_stream_types": {
          "type": "array",
          "items" : { "$ref" : "#/definitions/stream_type" }
        },

        // The target rate for this device. A different rate may be chosen if the driver does
        // not support the rate requested.
        "rate": "integer",

        // Gain value (in decibels) applied to device driver upon initialization.
        // If the key is not specified, a default gain value of 0.0 will be used.
        "driver_gain_db": "number",

        // Gain value (in decibels) applied to the software mix.
        // If the key is not specified, a software gain value of 0.0 will be used.
        "software_gain_db": "number"
      },
      "required": [ "device_id", "rate" ],
      "additionalProperties": false
    }
  },
  "type": "object",
  "properties": {
    "mix_profile": {
      "type": "object",
      "properties" : {
        "capacity_usec": { "type": "integer", "minimum": 1 },
        "deadline_usec": { "type": "integer", "minimum": 1 },
        "period_usec": { "type": "integer", "minimum": 1 }
      },
      "additionalProperties": false
    },
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
    "thermal_states": {
      "type" : "array",
      "items": {"$ref" : "#/definitions/thermal_state_format" }
    }
  },
  "required": ["volume_curve"],
  "additionalProperties": false
}
