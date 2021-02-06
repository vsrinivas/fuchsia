#[macro_use]
extern crate downcast;

use downcast::Any;

/* Trait */

trait Animal: Any {}

downcast!(Animal);

/* Impl */

struct Bird;

impl Animal for Bird {}

impl Bird {
    fn wash_beak(&self) {
        println!("Beak has been washed! What a clean beak!");
    }
}

/* Main */

fn main() {
    let animal: Box<Animal> = Box::new(Bird);
    {
        let bird = animal.downcast_ref::<Bird>().unwrap();
        bird.wash_beak();
    }
    let bird = animal.downcast::<Bird>().ok().unwrap();
    bird.wash_beak();
}
