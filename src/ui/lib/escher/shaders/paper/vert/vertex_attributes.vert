// Defines the names and types of vertex attributes attached to this program.

// TODO(fxbug.dev/7244): Should split this into another CL atop the current one, but
// I'm exhausted right now.
#error NOT_IMPLEMENTED

// Vertex attribute 0 is 'vec3 inPosition' by default, although this can be
// overridden.  Other attributes have no default type/name.
#ifdef VERTEX_ATTRIBUTE_0
layout(location = 0) in VERTEX_ATTRIBUTE_0;
#else
layout(location = 0) in vec3 inPosition;
#endif

#ifdef VERTEX_ATTRIBUTE_1
layout(location = 1) in VERTEX_ATTRIBUTE_1;
#endif

#ifdef VERTEX_ATTRIBUTE_2
layout(location = 2) in VERTEX_ATTRIBUTE_2;
#endif

#ifdef VERTEX_ATTRIBUTE_3
layout(location = 3) in VERTEX_ATTRIBUTE_3;
#endif

#ifdef VERTEX_ATTRIBUTE_4
layout(location = 4) in VERTEX_ATTRIBUTE_4;
#endif

#ifdef VERTEX_ATTRIBUTE_5
layout(location = 5) in VERTEX_ATTRIBUTE_5;
#endif

#ifdef VERTEX_ATTRIBUTE_6
layout(location = 6) in VERTEX_ATTRIBUTE_6;
#endif

#ifdef VERTEX_ATTRIBUTE_7
layout(location = 7) in VERTEX_ATTRIBUTE_7;
#endif

#ifdef VERTEX_ATTRIBUTE_8
layout(location = 8) in VERTEX_ATTRIBUTE_8;
#endif

#ifdef VERTEX_ATTRIBUTE_9
layout(location = 9) in VERTEX_ATTRIBUTE_9;
#endif

#ifdef VERTEX_ATTRIBUTE_10
layout(location = 10) in VERTEX_ATTRIBUTE_10;
#endif

#ifdef VERTEX_ATTRIBUTE_11
layout(location = 11) in VERTEX_ATTRIBUTE_11;
#endif

#ifdef VERTEX_ATTRIBUTE_12
layout(location = 12) in VERTEX_ATTRIBUTE_12;
#endif

#ifdef VERTEX_ATTRIBUTE_13
layout(location = 13) in VERTEX_ATTRIBUTE_13;
#endif

#ifdef VERTEX_ATTRIBUTE_14
layout(location = 14) in VERTEX_ATTRIBUTE_14;
#endif

#ifdef VERTEX_ATTRIBUTE_15
layout(location = 15) in VERTEX_ATTRIBUTE_15;
#endif
