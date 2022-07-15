# da7219

Audio hardware codec driver that complies with the fuchsia.hardware.audio/Codec
FIDL API. This driver creates 2 instances one for input and one for output.
The are Core and Driver classes, the 2 driver instances reference the same Core
object that implements functionality including the common interrupt handing that
gets used in both input and output instances.

## Testing

Unit tests for da7219 are available in the `da7219-tests`
package.

```
$ fx test da7219-tests
```

