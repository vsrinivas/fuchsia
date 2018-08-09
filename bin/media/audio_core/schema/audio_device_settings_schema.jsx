{
  "definitions": {
    "gain_settings": {
      "type": "object",
      "properties": {
        "db_gain": { "type": "number" },
        "mute":    { "type": "boolean" },
        "agc":     { "type": "boolean" }
      },
      "required": [ "db_gain", "mute", "agc" ],
      "additionalProperties" : false
    }
  },

  "type": "object",
  "properties": {
    "gain": { "$ref": "#/definitions/gain_settings" },

    // ignore_device:
    //
    // When true, causes the mixer to completely ignore this device.  The mixer
    // will close the device driver and not inform clients that it ever existed.
    "ignore_device": { "type": "boolean" },

    // disallow_auto_routing:
    //
    // When true, do not consider this device when applying automatic routing
    // policies.  For example, if the audio routing policy is set to "last
    // plugged", and this device is the last plugged device, it will still not
    // be declared the "default" device, and it will not have
    // audio outs/ins automatically attached to it.
    "disallow_auto_routing": { "type": "boolean" }
  },
  "required": [ "gain",
                "ignore_device",
                "disallow_auto_routing" ],
  "additionalProperties" : false
}
