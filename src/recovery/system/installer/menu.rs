// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use recovery_util::block::BlockDevice;

const CONST_SELECT_INSTALL_HEADLINE: &str = "Select Installation Method";
const CONST_BUTTON_USB_INSTALL: &str = "Install from USB";

const CONST_SELECT_DISK_HEADLINE: &str = "Select disk you would like to install Fuchsia to";

const CONST_WARNING_HEADLINE: &str = "WARNING:";
const CONST_WARN_MESSAGE: &str = "Installing Fuchsia will WIPE YOUR DISK!\nDo you wish to proceed?";

const CONST_PROGRESS_HEADLINE: &str = "Installing Fuchsia, Please Wait";

const CONST_SUCCESS_HEADLINE: &str = "Install Complete";

const CONST_ERR_HEADLINE: &str = "ERROR Cannot Install Fuchsia:";
const CONST_ERR_USER_DECLINE: &str = "User declined";
const CONST_ERR_NO_DISK: &str = "No available disks";
const CONST_ERR_UNEXPECTED_EVENT: &str = "Unexpected event";
const CONST_ERR_UNEXPECTED_INPUT: &str = "Unexpected input event";

const CONST_RESTART_REQUEST: &str = "Please restart your computer";

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum MenuState {
    SelectInstall,
    SelectDisk,
    Warning,
    Progress,
    Success,
    Error,
}

#[derive(Debug)]
pub enum MenuEvent {
    Navigate(Key),
    Enter,
    GotBlockDevices(Vec<BlockDevice>),
    ProgressUpdate(String),
    Success,
    Error(String),
}

#[derive(Debug, PartialEq)]
pub enum Key {
    Up,
    Down,
}

#[derive(Debug, PartialEq, Clone)]
pub enum MenuButtonType {
    USBInstall,
    Yes,
    No,
    Disk(BlockDevice),
    Restart,
    None,
}

impl MenuButtonType {
    pub fn to_str(&self) -> &str {
        match self {
            MenuButtonType::USBInstall => CONST_BUTTON_USB_INSTALL,
            MenuButtonType::Yes => "Yes",
            MenuButtonType::No => "No",
            MenuButtonType::Disk(ref dev) => &dev.topo_path,
            MenuButtonType::Restart => "Restart",
            MenuButtonType::None => "???",
        }
    }
}

pub struct MenuStateMachine {
    state: MenuState,
    heading: String,
    message: String,
    buttons: Vec<MenuButton>,
    selected_button_index: usize,
}

impl MenuStateMachine {
    pub fn new() -> MenuStateMachine {
        let mut new = MenuStateMachine {
            state: MenuState::SelectInstall,
            heading: String::new(),
            message: String::new(),
            buttons: Vec::new(),
            selected_button_index: 0,
        };
        new.setup_current_state();
        new
    }

    pub fn handle_event(&mut self, event: MenuEvent) -> MenuState {
        let new_state: MenuState = match self.state {
            MenuState::SelectInstall => match event {
                MenuEvent::Navigate(pressed_key) => self.button_cycle(pressed_key, self.state),
                MenuEvent::GotBlockDevices(devices) => {
                    self.add_block_device_buttons(devices);
                    if self.buttons.len() == 0 {
                        self.message = create_error_message(CONST_ERR_NO_DISK);
                        MenuState::Error
                    } else {
                        MenuState::SelectDisk
                    }
                }
                MenuEvent::Error(error_msg) => {
                    self.message = create_error_message(&error_msg);
                    MenuState::Error
                }
                MenuEvent::Enter => MenuState::SelectInstall,
                _ => {
                    self.message = create_error_message(CONST_ERR_UNEXPECTED_EVENT);
                    MenuState::Error
                }
            },
            MenuState::SelectDisk => match event {
                MenuEvent::Navigate(pressed_key) => self.button_cycle(pressed_key, self.state),
                MenuEvent::Enter => match self.get_selected_button_type() {
                    MenuButtonType::Disk(_) => MenuState::Warning,
                    _ => {
                        self.message = create_error_message(CONST_ERR_UNEXPECTED_INPUT);
                        MenuState::Error
                    }
                },
                MenuEvent::Error(error_msg) => {
                    self.message = create_error_message(&error_msg);
                    MenuState::Error
                }
                _ => {
                    self.message = create_error_message(CONST_ERR_UNEXPECTED_EVENT);
                    MenuState::Error
                }
            },
            MenuState::Warning => match event {
                MenuEvent::Navigate(pressed_key) => self.button_cycle(pressed_key, self.state),
                MenuEvent::Enter => match self.get_selected_button_type() {
                    MenuButtonType::Yes => MenuState::Progress,
                    MenuButtonType::No => {
                        self.message = create_error_message(CONST_ERR_USER_DECLINE);
                        MenuState::Error
                    }
                    _ => {
                        self.message = create_error_message(CONST_ERR_UNEXPECTED_INPUT);
                        MenuState::Error
                    }
                },
                MenuEvent::Error(error_msg) => {
                    self.message = create_error_message(&error_msg);
                    MenuState::Error
                }
                _ => {
                    self.message = create_error_message(CONST_ERR_UNEXPECTED_EVENT);
                    MenuState::Error
                }
            },
            MenuState::Progress => match event {
                MenuEvent::ProgressUpdate(update) => {
                    self.message = String::from(update);
                    MenuState::Progress
                }
                MenuEvent::Success => MenuState::Success,
                MenuEvent::Error(error_msg) => {
                    self.message = create_error_message(&error_msg);
                    MenuState::Error
                }
                _ => MenuState::Progress,
            },
            MenuState::Success => MenuState::Success,
            MenuState::Error => MenuState::Error,
        };

        if self.state != new_state {
            self.state = new_state;
            self.setup_current_state();
        }

        self.state
    }

    // Cycle selected button on keyboard up/down input
    pub fn button_cycle(&mut self, pressed_key: Key, state: MenuState) -> MenuState {
        let num_buttons = self.buttons.len();
        let selected_prev = self.selected_button_index.clone();

        let selected_next = match pressed_key {
            Key::Down => (selected_prev + 1) % num_buttons,
            Key::Up => {
                if selected_prev == 0 {
                    num_buttons - 1
                } else {
                    selected_prev - 1
                }
            }
        };

        self.buttons[selected_prev].selected = false;
        self.buttons[selected_next].selected = true;
        self.selected_button_index = selected_next;

        state
    }

    // Setup buttons & headings for current state
    fn setup_current_state(&mut self) {
        match self.state {
            MenuState::SelectInstall => {
                self.heading = String::from(CONST_SELECT_INSTALL_HEADLINE);

                self.buttons.clear();
                let mut install_buttons = installation_method_buttons();
                self.buttons.append(&mut install_buttons);
                self.selected_button_index = 0;
            }
            MenuState::SelectDisk => {
                self.heading = String::from(CONST_SELECT_DISK_HEADLINE);
            }
            MenuState::Warning => {
                self.heading = String::from(CONST_WARNING_HEADLINE);
                self.message = String::from(CONST_WARN_MESSAGE);

                self.buttons.clear();
                let mut warn_buttons = vec![
                    MenuButton::new(true, MenuButtonType::Yes),
                    MenuButton::new(false, MenuButtonType::No),
                ];

                self.buttons.append(&mut warn_buttons);
                self.selected_button_index = 0;
            }
            MenuState::Progress => {
                self.buttons.clear();
                self.heading = String::from(CONST_PROGRESS_HEADLINE);
            }
            MenuState::Success => {
                self.buttons.clear();
                self.buttons.push(MenuButton::new(true, MenuButtonType::Restart));
                self.selected_button_index = 0;
                self.heading = String::from(CONST_SUCCESS_HEADLINE);
                self.message = String::from(CONST_RESTART_REQUEST);
            }
            MenuState::Error => {
                self.buttons.clear();
                self.buttons.push(MenuButton::new(true, MenuButtonType::Restart));
                self.selected_button_index = 0;
                self.heading = String::from(CONST_ERR_HEADLINE);
            }
        };
    }

    fn add_block_device_buttons(&mut self, devices: Vec<BlockDevice>) {
        self.buttons.clear();

        for device in devices {
            let disk_button = MenuButton::new(false, MenuButtonType::Disk(device.clone()));
            self.buttons.push(disk_button);
        }

        if self.buttons.len() > 0 {
            self.buttons[0].selected = true;
            self.selected_button_index = 0;
        }
    }

    pub fn get_state(&self) -> MenuState {
        self.state
    }

    pub fn get_heading(&self) -> String {
        self.heading.clone()
    }

    pub fn get_buttons(&self) -> Vec<MenuButton> {
        self.buttons.clone()
    }

    pub fn get_selected_button_type(&self) -> &MenuButtonType {
        if self.buttons.len() == 0 {
            return &MenuButtonType::None;
        }

        &self.buttons[self.selected_button_index].button_type
    }

    pub fn get_message(&self) -> String {
        self.message.clone()
    }
}

#[derive(Debug, PartialEq, Clone)]
pub struct MenuButton {
    selected: bool,
    button_type: MenuButtonType,
}

impl MenuButton {
    pub fn new(selected: bool, button_type: MenuButtonType) -> MenuButton {
        MenuButton { selected, button_type }
    }

    pub fn get_text(&self) -> String {
        self.button_type.to_str().to_owned()
    }

    pub fn is_selected(&self) -> bool {
        self.selected
    }
}

fn create_error_message(error: &str) -> String {
    format!("{}\n{}", error, CONST_RESTART_REQUEST)
}

fn installation_method_buttons() -> Vec<MenuButton> {
    let install_buttons = vec![MenuButton::new(true, MenuButtonType::USBInstall)];

    install_buttons
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::MenuEvent::Navigate;

    fn make_block_device() -> BlockDevice {
        BlockDevice {
            class_path: "/dev/class/block/001".to_owned(),
            topo_path: "/dev/hello".to_owned(),
            size: 0,
        }
    }

    #[test]
    fn test_create_menu() -> std::result::Result<(), anyhow::Error> {
        let menu = MenuStateMachine::new();
        let state = menu.get_state();
        assert_eq!(state, MenuState::SelectInstall);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_SELECT_INSTALL_HEADLINE);
        let selected_index = menu.selected_button_index;
        assert_eq!(selected_index, 0);
        let selected_type = menu.get_selected_button_type();
        assert_eq!(selected_type, &MenuButtonType::USBInstall);
        let buttons = menu.get_buttons();
        assert_eq!(buttons.len(), 1);
        let button = buttons.first().unwrap();
        assert_eq!(button.get_text(), CONST_BUTTON_USB_INSTALL);
        Ok(())
    }

    #[test]
    fn test_navigate_install_screen() -> std::result::Result<(), anyhow::Error> {
        let mut menu = MenuStateMachine::new();
        menu.handle_event(Navigate(Key::Up));
        let state = menu.get_state();
        assert_eq!(state, MenuState::SelectInstall);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_SELECT_INSTALL_HEADLINE);
        let selected_index = menu.selected_button_index;
        assert_eq!(selected_index, 0);
        let selected_type = menu.get_selected_button_type();
        assert_eq!(selected_type, &MenuButtonType::USBInstall);
        let buttons = menu.get_buttons();
        assert_eq!(buttons.len(), 1);
        let button = buttons.first().unwrap();
        assert_eq!(button.get_text(), CONST_BUTTON_USB_INSTALL);
        menu.handle_event(Navigate(Key::Down));
        let state = menu.get_state();
        assert_eq!(state, MenuState::SelectInstall);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_SELECT_INSTALL_HEADLINE);
        let selected_index = menu.selected_button_index;
        assert_eq!(selected_index, 0);
        let selected_type = menu.get_selected_button_type();
        assert_eq!(selected_type, &MenuButtonType::USBInstall);
        let buttons = menu.get_buttons();
        assert_eq!(buttons.len(), 1);
        let button = buttons.first().unwrap();
        assert_eq!(button.get_text(), CONST_BUTTON_USB_INSTALL);

        Ok(())
    }

    #[test]
    fn test_select_usb_install() -> std::result::Result<(), anyhow::Error> {
        let mut menu = MenuStateMachine::new();
        menu.handle_event(MenuEvent::Enter);
        let state = menu.get_state();
        assert_eq!(state, MenuState::SelectInstall);
        menu.handle_event(MenuEvent::GotBlockDevices(vec![make_block_device()]));
        let state = menu.get_state();
        assert_eq!(state, MenuState::SelectDisk);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_SELECT_DISK_HEADLINE);
        let selected_index = menu.selected_button_index;
        assert_eq!(selected_index, 0);
        let selected_type = menu.get_selected_button_type();
        assert_eq!(selected_type, &MenuButtonType::Disk(make_block_device()));
        let buttons = menu.get_buttons();
        assert_eq!(buttons.len(), 1);
        let button = buttons.first().unwrap();
        assert_eq!(button.get_text(), String::from("/dev/hello"));

        Ok(())
    }

    #[test]
    fn test_select_disk() -> std::result::Result<(), anyhow::Error> {
        let mut menu = MenuStateMachine::new();
        menu.handle_event(MenuEvent::Enter);
        menu.handle_event(MenuEvent::GotBlockDevices(vec![make_block_device()]));
        menu.handle_event(MenuEvent::Enter);
        let state = menu.get_state();
        assert_eq!(state, MenuState::Warning);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_WARNING_HEADLINE);
        let buttons = menu.get_buttons();
        assert_eq!(buttons.len(), 2);
        let button = buttons.first().unwrap();
        assert_eq!(button.get_text(), String::from("Yes"));
        let button = buttons.get(1).unwrap();
        assert_eq!(button.get_text(), String::from("No"));

        Ok(())
    }

    #[test]
    fn test_user_agrees() -> std::result::Result<(), anyhow::Error> {
        let mut menu = MenuStateMachine::new();
        menu.handle_event(MenuEvent::Enter);
        menu.handle_event(MenuEvent::GotBlockDevices(vec![make_block_device()]));
        menu.handle_event(MenuEvent::Enter);
        menu.handle_event(MenuEvent::Enter);
        let state = menu.get_state();
        assert_eq!(state, MenuState::Progress);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_PROGRESS_HEADLINE);
        Ok(())
    }

    #[test]
    fn test_user_declines() -> std::result::Result<(), anyhow::Error> {
        let mut menu = MenuStateMachine::new();
        menu.handle_event(MenuEvent::Enter);
        menu.handle_event(MenuEvent::GotBlockDevices(vec![make_block_device()]));
        menu.handle_event(MenuEvent::Enter);
        menu.handle_event(MenuEvent::Navigate(Key::Down));
        menu.handle_event(MenuEvent::Enter);
        let state = menu.get_state();
        assert_eq!(state, MenuState::Error);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_ERR_HEADLINE);
        let message = menu.get_message();
        assert_eq!(message, format!("{}\n{}", CONST_ERR_USER_DECLINE, CONST_RESTART_REQUEST));
        Ok(())
    }

    #[test]
    fn test_no_block_devices() -> std::result::Result<(), anyhow::Error> {
        let mut menu = MenuStateMachine::new();
        menu.handle_event(MenuEvent::Enter);
        let state = menu.get_state();
        assert_eq!(state, MenuState::SelectInstall);
        menu.handle_event(MenuEvent::GotBlockDevices(Vec::new()));
        let state = menu.get_state();
        assert_eq!(state, MenuState::Error);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_ERR_HEADLINE);
        let message = menu.get_message();
        assert_eq!(message, format!("{}\n{}", CONST_ERR_NO_DISK, CONST_RESTART_REQUEST));
        let buttons = menu.get_buttons();
        assert_eq!(buttons.len(), 0);
        Ok(())
    }
}
