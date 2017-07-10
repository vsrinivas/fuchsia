// TODO: remove this
use mxruntime::{get_service_root, connect_to_environment_service};
use magenta::{Channel, ChannelOpts, HandleBase};
use application_services_service_provider::*;
use application_services::*;
#[cfg(target_arch = "x86_64")]
use std::panic;

/// Produces a proper backtrace on panic by triggering a software breakpoint
/// with a special value in a register that causes it to print a backtrace and
/// then resume execution, which then runs the old panic handler.
#[cfg(target_arch = "x86_64")]
pub fn install_panic_backtrace_hook() {
    let old_hook = panic::take_hook();
    panic::set_hook(Box::new(move |arg| {
        unsafe {
            // constant comes from CRASHLOGGER_RESUME_MAGIC in crashlogger.h
            asm!("mov $$0xee726573756d65ee, %rax; int3" : : : "rax" : "volatile");
        }
        old_hook(arg)
    }));
}

#[cfg(not(target_arch = "x86_64"))]
pub fn install_panic_backtrace_hook() {}

pub struct ApplicationContext {
    pub environment: ApplicationEnvironment_Proxy,
    pub environment_services: ServiceProvider_Proxy,
}

// pub type ApplicationContextPtr = Arc<Mutex<ApplicationContext>>;

impl ApplicationContext {
    pub fn new() -> ApplicationContext {
        let service_root = get_service_root().unwrap();
        let application_environment_channel =
            connect_to_environment_service(service_root,
                                           ApplicationEnvironment_Metadata::SERVICE_NAME)
                .unwrap();
        let application_environment_client =
            ApplicationEnvironment_Client::from_handle(application_environment_channel.into_handle());
        let mut proxy = ApplicationEnvironment_new_Proxy(application_environment_client);
        let (service_provider, service_provider_server) = ServiceProvider_new_pair();
        proxy.get_services(service_provider_server);
        ApplicationContext {
            environment: proxy,
            environment_services: service_provider,
        }
    }
}

pub struct Launcher {
    proxy: ApplicationLauncher_Proxy,
}

impl Launcher {
    pub fn new(context: &mut ApplicationContext) -> Launcher {
        let (s1, s2) = Channel::create(ChannelOpts::Normal).unwrap();
        context.environment_services.connect_to_service(ApplicationLauncher_Metadata::SERVICE_NAME.to_owned(), s2);
        let launcher_client = ApplicationLauncher_Client::from_handle(s1.into_handle());
        let proxy = ApplicationLauncher_new_Proxy(launcher_client);
        Launcher { proxy }
    }

    pub fn launch(&mut self, url: String, arguments: Option<Vec<String>>) -> App {
        let (services_proxy, services_request) = ServiceProvider_new_pair();
        let (controller_proxy, controller_request) = ApplicationController_new_pair();

        let launch_info = ApplicationLaunchInfo {
            url,
            arguments,
            service_request: None,
            flat_namespace: None,
            services: Some(services_request)
        };
        self.proxy.create_application(launch_info, Some(controller_request));
        App { services: services_proxy, controller: controller_proxy }
    }
}

pub struct App {
    pub services: ServiceProvider_Proxy,
    pub controller: ApplicationController_Proxy,
}
