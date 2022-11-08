Working examples are put in this directory. They can be run with `cargo run --example`.

```sh
echo '{"hello": "world"}' | cargo run --example parse
echo '["foo",  42,    null ]' | cargo run --example minify
cargo run --example json_value
```

