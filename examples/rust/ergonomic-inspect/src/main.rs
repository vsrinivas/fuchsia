// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod quick_start_before {
    // [START quick_start_before_decl]
    struct Yak {
        // TODO: Overflow risk at high altitudes?
        hair_length: u16,       // Current hair length in mm
        credit_card_no: String, // Super secret
    }

    impl Yak {
        pub fn new() -> Self {
            Self { hair_length: 5, credit_card_no: "<secret>".to_string() }
        }

        pub fn shave(&mut self) {
            self.hair_length = 0;
        }
    }
    // [END quick_start_before_decl]

    #[test]
    fn init() {
        // [START quick_start_before_init]
        let mut yak = Yak::new();
        yak.shave();
        // [END quick_start_before_init]
        let _ = yak.credit_card_no; // Prevent unused warning
        assert_eq!(yak.hair_length, 0);
    }
}

mod quick_start_after {
    use fuchsia_inspect::{assert_inspect_tree, NumericProperty};

    // [START quick_start_after_decl]
    use fuchsia_inspect_derive::{
        IValue,      // A RAII smart pointer that can be attached to inspect
        Inspect,     // The core trait and derive-macro
        WithInspect, // Provides `.with_inspect(..)`
    };

    #[derive(Inspect)]
    struct Yak {
        #[inspect(rename = "hair_length_mm")] // Clarify that it's millimeters
        hair_length: IValue<u16>, // Encapsulate primitive in IValue

        #[inspect(skip)] // Credit card number should NOT be exposed
        credit_card_no: String,
        shaved_counter: fuchsia_inspect::UintProperty, // Write-only counter
        inspect_node: fuchsia_inspect::Node,           // Inspect node of this Yak, optional
    }

    impl Yak {
        pub fn new() -> Self {
            Self {
                hair_length: IValue::new(5), // Or if you prefer, `5.into()`
                credit_card_no: "<secret>".to_string(),

                // Inspect nodes and properties should be default-initialized
                shaved_counter: fuchsia_inspect::UintProperty::default(),
                inspect_node: fuchsia_inspect::Node::default(),
            }
        }

        pub fn shave(&mut self) {
            self.hair_length.iset(0); // Set the source value AND update the inspect property
            self.shaved_counter.add(1u64); // Increment counter
        }
    }
    // [END quick_start_after_decl]

    #[allow(dead_code)]
    impl Yak {
        fn credit_card_no(&self) -> &str {
            &self.credit_card_no
        }
    }

    #[test]
    fn init() -> Result<(), fuchsia_inspect_derive::AttachError> {
        let inspector = fuchsia_inspect::Inspector::new();

        // [START quick_start_after_init]
        // Initialization
        let mut yak = Yak::new()
            .with_inspect(/* parent node */ inspector.root(), /* name */ "my_yak")?;

        assert_inspect_tree!(inspector, root: {
            my_yak: { hair_length_mm: 5u64, shaved_counter: 0u64 }
        });

        // Mutation
        yak.shave();
        assert_inspect_tree!(inspector, root: {
            my_yak: { hair_length_mm: 0u64, shaved_counter: 1u64 }
        });

        // Destruction
        std::mem::drop(yak);
        assert_inspect_tree!(inspector, root: {});
        // [END quick_start_after_init]

        Ok(())
    }
}

mod derive_inspect {
    use fuchsia_inspect::{assert_inspect_tree, Node};
    use fuchsia_inspect_derive::{AttachError, IValue, Inspect, WithInspect};
    use std::cell::RefCell;

    // [START inspect_node_present_decl]
    #[derive(Inspect)]
    struct Yak {
        name: IValue<String>,
        age: IValue<u16>,
        inspect_node: fuchsia_inspect::Node, // NOTE: Node is present
    }

    // Yak is represented as a node with `name` and `age` properties.
    // [END inspect_node_present_decl]

    impl Yak {
        fn new() -> Self {
            Self {
                name: "Lil Sebastian".to_string().into(),
                age: 3.into(),
                inspect_node: Node::default(),
            }
        }
    }

    // [START inspect_node_absent_decl]
    #[derive(Inspect)]
    struct YakName {
        title: IValue<String>, // E.g. "Lil"
        full_name: IValue<String>, // E.g. "Sebastian"
                               // NOTE: Node is absent
    }

    // YakName has no separate node. Instead, the `title` and `full_name`
    // properties are attached directly to the parent node.
    // [END inspect_node_absent_decl]

    // [START inspect_forward_decl]
    #[derive(Inspect)]
    struct Wrapper {
        #[inspect(forward)]
        inner: RefCell<Inner>,
    }

    #[derive(Inspect)]
    struct Inner {
        name: IValue<String>,
        age: IValue<u16>,
        inspect_node: fuchsia_inspect::Node,
    }

    // Wrapper is represented as a node with `name` and `age` properties.
    // [END inspect_forward_decl]

    enum Horse {
        Icelandic,
        #[allow(dead_code)]
        Arabian,
    }

    impl Inspect for &mut Horse {
        fn iattach(self, _parent: &Node, _name: impl AsRef<str>) -> Result<(), AttachError> {
            // Exercise
            Ok(())
        }
    }

    // [START inspect_nested_decl]
    // Stable is represented as a node with two child nodes `yak` and `horse`
    #[derive(Inspect)]
    struct Stable {
        yak: Yak,     // Yak derives Inspect
        horse: Horse, // Horse implements Inspect manually
        inspect_node: fuchsia_inspect::Node,
    }
    // [END inspect_nested_decl]

    impl Stable {
        fn new() -> Self {
            Self { yak: Yak::new(), horse: Horse::Icelandic, inspect_node: Node::default() }
        }
    }

    #[test]
    fn attach_yak() -> Result<(), AttachError> {
        let inspector = fuchsia_inspect::Inspector::new();

        // [START inspect_node_present_init]
        let yak = Yak::new().with_inspect(inspector.root(), "my_yak")?;
        assert_inspect_tree!(inspector, root: { my_yak: { name: "Lil Sebastian", age: 3u64 }});
        // [END inspect_node_present_init]

        let _ = yak; // Suppress unused warning
        Ok(())
    }

    #[test]
    fn attach_stable() -> Result<(), AttachError> {
        let inspector = fuchsia_inspect::Inspector::new();

        // [START inspect_nested_init]
        // Stable owns a Yak, which also implements Inspect.
        let stable = Stable::new().with_inspect(inspector.root(), "stable")?;
        assert_inspect_tree!(inspector,
            root: { stable: { yak: { name: "Lil Sebastian", age: 3u64 }}});
        // [END inspect_nested_init]

        let _ = stable; // Suppress unused warning
        Ok(())
    }
}

mod smart_pointers {
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::{AttachError, IValue, WithInspect};

    #[test]
    fn ivalue_demo() -> Result<(), AttachError> {
        let inspector = fuchsia_inspect::Inspector::new();

        // [START smart_pointers_ivalue]
        let mut number = IValue::new(1337u16) // IValue is an IOwned smart pointer
            .with_inspect(inspector.root(), "my_number")?; // Attach to inspect tree

        // Dereference the value behind the IValue, without mutating
        assert_eq!(*number, 1337u16);
        {
            // Mutate value behind an IOwned smart pointer, using a scope guard
            let mut number_guard = number.as_mut();
            *number_guard = 1338;
            *number_guard += 1;

            // Inspect state not yet updated
            assert_inspect_tree!(inspector, root: { my_number: 1337u64 });
        }
        // When the guard goes out of scope, the inspect state is updated
        assert_inspect_tree!(inspector, root: { my_number: 1339u64 });

        number.iset(1340); // Sets the source value AND updates inspect
        assert_inspect_tree!(inspector, root: { my_number: 1340u64 });

        let inner = number.into_inner(); // Detaches from inspect tree...
        assert_eq!(inner, 1340u16); // ...and returns the inner value.
                                    // [END smart_pointers_ivalue]

        assert_inspect_tree!(inspector, root: {});

        Ok(())
    }
}

mod unit {
    use fuchsia_inspect::assert_inspect_tree;
    use fuchsia_inspect_derive::{AttachError, IValue, WithInspect};

    use fuchsia_inspect_derive::Unit;

    // [START unit_plain_decl]
    // Represented as a Node with two properties, `x` and `y`, of type UintProperty
    #[derive(Unit)]
    struct Point {
        x: f32,
        y: f32,
    }
    // [END unit_plain_decl]

    // [START unit_nested_decl]
    // Represented as a Node with two child nodes `top_left` and `bottom_right`
    #[derive(Unit)]
    struct Rect {
        #[inspect(rename = "top_left")]
        tl: Point,

        #[inspect(rename = "bottom_right")]
        br: Point,
    }
    // [END unit_nested_decl]

    #[test]
    fn unit_demo() -> Result<(), AttachError> {
        let inspector = fuchsia_inspect::Inspector::new();

        // [START unit_nested_init]
        let rect_original = Rect { tl: Point { x: -3.0, y: 5.0 }, br: Point { x: 2.0, y: -1.5 } };
        let rect = IValue::new(rect_original) // IValue wraps Rect, because it implements `Unit`
            .with_inspect(inspector.root(), "rectangle")?;

        assert_inspect_tree!(inspector, root: { rectangle: {
            top_left: { x: -3f64, y: 5f64 },
            bottom_right: { x: 2f64, y: -1.5f64 },
        }});
        // [END unit_nested_init]

        std::mem::drop(rect);
        assert_inspect_tree!(inspector, root: {});

        Ok(())
    }
}
