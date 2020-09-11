# mold. The parallel CPU vector renderer


*mold* is a highly efficient vector content renderer and composer that makes use of all processor cores and vector units.

It currently supports:

* non-zero or even-odd paths
* lines, quadratic and cubic Bézier curves and their rational counterparts
* RGB solid fills with opacity
* correct gamma blending
* efficient per-path transforms

The renderer achieves its performance through a few principles that are present throughout the code-base:

* tiled renderer can render simple tiles efficiently
* long expensive buffer operations are parallelized on multiple threads
* partial results are stored in temporary buffers and the whole renderer runs in a multi-stage pipeline
* tight loops are as branchless as possible and are easily auto-vectorized
* high-quality efficient linear-to-sRGB approximation

## Getting started with mold

* read the mold [API reference](https://fuchsia-docs.firebaseapp.com/rust/mold/)
* check out the [Carnelian clockface example](https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/src/lib/ui/carnelian/examples/clockface.rs)

## Example

```rust
use mold::{Buffer, Composition, Fill, Point, Style};

fn main() {
    const WIDTH: usize = 256;
    const HEIGHT: usize = 256;

    let mut buffer = vec![[255u8; 4]; WIDTH * HEIGHT];
    let mut composition = Composition::new();

    let mut triangle_path = Path::new();
    triangle_path.line(
        Point::new(100.0, 100.0),
        Point::new(150.0, 200.0),
    );
    triangle_path.line(
        Point::new(150.0, 200.0),
        Point::new(200.0, 100.0),
    );
    triangle_path.line(
        Point::new(200.0, 100.0),
        Point::new(100.0, 100.0),
    );

    let layer_id = composition.create_layer().expect("layer limit reached");

    composition.insert_in_layer(layer_id, &triangle_path).set_style(Style {
        fill: Fill::Solid([1.0, 0.0, 0.0, 1.0]),
        ..Default::default()
    });

    composition.render(
        Buffer {
            buffer: &mut buffer,
            width: WIDTH,
            ..Default::default()
        },
        [1.0, 1.0, 1.0, 1.0],
        None,
    );

}
```
