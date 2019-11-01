# Paper Shader Compiler

This tool compiles all the shaders used by Escher's paper renderer. The resulting compiled shader spirv
binary is then saved to disk in src/ui/lib/escher/shaders/spirv. The name for the file is auto_generated
based on the input name of the original shader plus a hash value calculated from the list of shader
variant arguments.

To use:

1) Migrate to your fuchsia root directory.

2) fx set terminal.x64 --with //garnet/packages/examples:escher --args escher_use_null_vulkan_config_on_host=false

3) fx build host_x64/paper_shader_compiler && out/default/host_x64/paper_shader_compiler
