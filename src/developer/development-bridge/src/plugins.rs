#[macro_export]
macro_rules! plugins {
    ($remote:expr, $sub:ident) => {
        plugins!(
            $remote, $sub,
            ffx_run_component, RunComponentCommand => run_component,
            ffx_test, TestCommand => test,
        )
    };
    ($remote:expr, $sub:ident, $($library:ident, $cmd:ident => $func:ident, )+) => {
        match $sub {
            $(
            Subcommand::$cmd(c) => $library::$func($remote, c).await,
            )+
            _ => Err(anyhow!("No integration found"))
        }
    };
}
