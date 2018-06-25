{
  "definitions": {
    "gain_settings": {
      "type": "object",
      "properties": {
        "db_gain": { "type": "number" },
        "mute":    { "type": "boolean" },
        "agc":     { "type": "boolean" }
      },
      "required": [ "db_gain", "mute", "agc" ]
    }
  },

  "type": "object",
  "properties": {
    "gain": { "$ref": "#/definitions/gain_settings" }
  },
  "required": [ "gain" ]
}
