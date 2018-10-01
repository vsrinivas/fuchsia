#ifdef inPosition
#error Testing that we can detect these sorts of definitions.  DEF
#else
#error Testing that we can detect these sorts of definitions.  NDEF
#endif

#ifdef SHADOW_VOLUME_EXTRUDE
#ifndef inShouldExtrude
#error Shadow volume extrusion requires inShouldExtrude vertex attribute.

vec4 ComputeWorldSpacePosition(vec4 model_space_position) {
  vec4 world_space_position = model_transform * model_space_position;
  if (inShouldExtrude == 0) {
    return world_space_position;
  } else {
    vec4 extrusion_dir = normalize(world_space_position - light_position);
    return world_space_position + extrusion_length * extrusion_dir;
  }
}

#else  // Default world space position.

vec4 ComputeWorldSpacePosition(vec4 model_space_position) {
  return model_transform * model_space_position;
}

#endif
