use cs2::*;
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(
    name = "Component Statistics v2 (cs2) Reporting Tool",
    about = "Displays information about components on the system."
)]
struct Opt {
    /// Path to HubV2 of a root component
    #[structopt()]
    hub_v2_path: PathBuf,
}

fn main() {
    let opt = Opt::from_args();
    let lines = Component::new_root_component(opt.hub_v2_path).generate_output();
    let output = lines.join("\n");
    println!("{}", output);
}
