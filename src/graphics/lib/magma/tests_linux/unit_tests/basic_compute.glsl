#version 450

layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout (binding = 0) buffer buf
{
  uint data[];
};

void main() {
  data[gl_GlobalInvocationID.x] = gl_GlobalInvocationID.x;
}
