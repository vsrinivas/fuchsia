# type_hash

Generate a hash for a Rust type.

The primary use-case for this crate is for detecting differences in message
types between versions of a crate.

The `TypeHash` trait is implemented for most built-in types and a derive macro
is provided, for implementing it for your own types.

## Examples

```rust
use type_hash::TypeHash;

#[derive(TypeHash)]
pub enum Message {
    LaunchMissiles { destination: String },
    CancelMissiles,
}

fn main() {
    let hash = Message::type_hash();
    // this will only change if the type definition changes
    assert_eq!(hash, 11652809455620829461);
}
```

## Customising derived TypeHash implementations

### `#[type_hash(foreign_type)]`

If a struct field has a foreign type that does not implement `TypeHash`, you can
mark it as a foreign type and the derive `TypeHash` implementation will use the
name of the type in the hash instead. You need to be a little bit careful here
because a change in the third party crate could change your type in an undetectable
way.

```rust
#[derive(TypeHash)]
pub struct MyStruct {
    #[type_hash(foreign_type)]
    data: ArrayVec<[u16; 7]>
}
```

### `#[type_hash(skip)]`

Skip a field, so it is not part of the hash.

```rust
#[derive(TypeHash)]
pub struct MyStruct {
    #[type_hash(skip)]
    not_important: Vec<i64>,
}
```

### `#[type_hash(as = "...")]`

Hash a field as if it had a different type. This allows you to change the
type of a field to a different type that is still compatible for your
application, without affecting the hash.

```rust
#[derive(TypeHash)]
pub struct MyStruct {
    #[type_hash(as = "HashSet<i64>")]
    numbers: BTreeSet<i64>,
}
```
