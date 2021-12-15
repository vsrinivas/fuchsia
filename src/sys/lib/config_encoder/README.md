# `config_encoder`

This crate performs the final structured configuration resolution and encoding
for a component. This process involves combining the component manifest's
declared config schema with a packaged value file, then encoding the resulting
fields into a FIDL struct.
