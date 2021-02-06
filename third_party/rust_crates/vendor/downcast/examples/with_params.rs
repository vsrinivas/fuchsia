#[macro_use]
extern crate downcast;

use downcast::Any;
use std::fmt::Debug;

/* Trait */

trait Animal<X>: Any where X: Debug {}

downcast!(<X> Animal<X> where X: Debug);

/* Impl */

struct Bird;

impl Animal<usize> for Bird {}

impl Bird {
    fn wash_beak(&self) {
        println!("Beak has been washed! What a clean beak!");
    }
}

/* Main */

fn main() {
    let animal: Box<Animal<usize>> = Box::new(Bird);
    {
        let bird = animal.downcast_ref::<Bird>().unwrap();
        bird.wash_beak();
    }
    let bird = animal.downcast::<Bird>().ok().unwrap();
    bird.wash_beak();
}
