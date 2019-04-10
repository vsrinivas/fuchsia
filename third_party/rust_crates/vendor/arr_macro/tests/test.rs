#[cfg(test)]
mod tests {
    use arr_macro::arr;

    #[test]
    fn main_test() {
        let x: [Option<String>; 3] = arr![None; 3];
        assert_eq!(
            [None, None, None],
            x
        );

        // works with all enum types (and impl copy is not required)
        #[allow(dead_code)]
        enum MyEnum {
            A,
            B
        }
        let _: [MyEnum; 33] = arr![MyEnum::A; 33];

        // Vec::new()
        let _: [Vec<String>; 33] = arr![Vec::new(); 33];

        // or your own struct type
        // and you can even use a counter to behave differently based on the array index
        #[derive(Debug)]
        struct MyStruct {
            member: u16,
        }
        impl MyStruct {
            fn new(member: u16) -> Self {
                MyStruct { member }
            }
        }
        let mut i = 0u16;
        let x: [MyStruct; 33] = arr![MyStruct::new({i += 1; i - 1}); 33];

        assert_eq!(0, x[0].member);
        assert_eq!(1, x[1].member);
        assert_eq!(2, x[2].member);
    }
}
