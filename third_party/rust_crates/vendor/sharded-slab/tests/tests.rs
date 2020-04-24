use sharded_slab::Slab;
#[test]
fn big() {
    let slab = Slab::new();

    for i in 0..10000 {
        println!("{:?}", i);
        let k = slab.insert(i).expect("insert");
        assert_eq!(slab.get(k).expect("get"), i);
    }
}

#[test]
fn custom_page_sz() {
    struct TinyConfig;

    impl sharded_slab::Config for TinyConfig {
        const INITIAL_PAGE_SIZE: usize = 8;
    }
    let slab = Slab::new_with_config::<TinyConfig>();

    for i in 0..4096 {
        println!("{}", i);
        let k = slab.insert(i).expect("insert");
        assert_eq!(slab.get(k).expect("get"), i);
    }
}
