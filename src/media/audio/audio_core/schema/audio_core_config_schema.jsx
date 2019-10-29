{
  "definitions": {
    "volume_mapping": {
      "type": "object",
      "properties": {
        "level" : "number",
        "db": "number"
      },
      "required": ["level", "db"]
    }
   },

  "type": "object",
  "properties": {
    "volume_curve": {
      "type": "array",
      "items": { "$ref": "#/definitions/volume_mapping" }
    }
  },
  "required": ["volume_curve"]
}