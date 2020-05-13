# A library for handling internationalized strings.

## Using in `BUILD.gn`

```
rustc_binary("binary_name") {
  deps = [
    # ...
	"//src/lib/intl/strings:lib",
	# ...
  ]
}
```

## Example `use` clause in rust code

```
use intl_messages::parser;
```


## Running tests

```
fx test //src/lib/intl/strings
```

