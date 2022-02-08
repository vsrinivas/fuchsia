// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const CONST_SELECT_INSTALL_HEADLINE: &'static str = "Select Installation Method";
const CONST_SELECT_DISK_HEADLINE: &'static str = "Select Disk you would like to install Fuchsia to";
const CONST_WARNING_HEADLINE: &'static str = "WARNING: ";
const CONST_PROGRESS_MSG: &'static str = "Installing Fuchsia";

const CONST_ERR_HEADLINE: &'static str = "ERROR Cannot install Fuchsia:";
const CONST_ERR_USER_DECLINE: &'static str = "User declined";
const CONST_ERR_UNEXPECTED_INPUT: &'static str = "Unexpected Input Event";
pub const CONST_ERR_RESTART: &'static str = "Please Restart your Computer";

const CONST_BUTTON_USB_INSTALL: &'static str = "Install from USB";

pub const CONST_WARN_MESSAGE: &'static str = "Installing Fuchsia will WIPE YOUR DISK!";
pub const CONST_WARN_PROCEED: &'static str = "Do you wish to proceed?";

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum MenuState {
    SelectInstall,
    SelectDisk,
    Warning,
    Progress,
    Error,
}

#[derive(Debug)]
pub enum MenuEvent {
    Navigate(Key),
    Enter,
}

#[derive(Debug, PartialEq)]
pub enum Key {
    Up,
    Down,
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum MenuButtonType {
    USBInstall,
    Yes,
    No,
    Disk,
    None,
}

pub struct MenuStateMachine {
    state: MenuState,
    heading: String,
    error_msg: String,
    buttons: Vec<MenuButton>,
    selected_button_index: usize,
}

impl MenuStateMachine {
    pub fn new() -> MenuStateMachine {
        let mut new = MenuStateMachine {
            state: MenuState::SelectInstall,
            heading: String::new(),
            error_msg: String::new(),
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
                MenuEvent::Enter => match self.get_selected_button_type() {
                    MenuButtonType::USBInstall => MenuState::SelectDisk,
                    _ => {
                        self.error_msg = String::from(CONST_ERR_UNEXPECTED_INPUT);
                        MenuState::Error
                    }
                },
            },
            MenuState::SelectDisk => match event {
                MenuEvent::Navigate(pressed_key) => self.button_cycle(pressed_key, self.state),
                MenuEvent::Enter => match self.get_selected_button_type() {
                    MenuButtonType::Disk => MenuState::Warning,
                    _ => {
                        self.error_msg = String::from(CONST_ERR_UNEXPECTED_INPUT);
                        MenuState::Error
                    }
                },
            },
            MenuState::Warning => match event {
                MenuEvent::Navigate(pressed_key) => self.button_cycle(pressed_key, self.state),
                MenuEvent::Enter => match self.get_selected_button_type() {
                    MenuButtonType::Yes => MenuState::Progress,
                    MenuButtonType::No => {
                        self.error_msg = String::from(CONST_ERR_USER_DECLINE);
                        MenuState::Error
                    }
                    _ => {
                        self.error_msg = String::from(CONST_ERR_UNEXPECTED_INPUT);
                        MenuState::Error
                    }
                },
            },
            MenuState::Progress => MenuState::Progress,
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
        self.buttons.clear();

        match self.state {
            MenuState::SelectInstall => {
                self.heading = String::from(CONST_SELECT_INSTALL_HEADLINE);

                let mut install_buttons = installation_method_buttons();
                self.buttons.append(&mut install_buttons);
                self.selected_button_index = 0;
            }
            MenuState::SelectDisk => {
                self.heading = String::from(CONST_SELECT_DISK_HEADLINE);

                let mut disk_buttons = vec![
                    MenuButton::new(String::from("Placeholder Disk 1"), true, MenuButtonType::Disk),
                    MenuButton::new(
                        String::from("Placeholder Disk 2"),
                        false,
                        MenuButtonType::Disk,
                    ),
                    MenuButton::new(
                        String::from("Placeholder Disk 3"),
                        false,
                        MenuButtonType::Disk,
                    ),
                ];
                self.buttons.append(&mut disk_buttons);
                self.selected_button_index = 0;
            }
            MenuState::Warning => {
                self.heading = String::from(CONST_WARNING_HEADLINE);

                self.buttons.clear();
                let mut warn_buttons = vec![
                    MenuButton::new(String::from("Yes"), true, MenuButtonType::Yes),
                    MenuButton::new(String::from("No"), false, MenuButtonType::No),
                ];

                self.buttons.append(&mut warn_buttons);
                self.selected_button_index = 0;
            }
            MenuState::Progress => {
                self.heading = String::from(CONST_PROGRESS_MSG);
            }
            MenuState::Error => {
                self.heading = String::from(CONST_ERR_HEADLINE);
                self.buttons.clear();
            }
        };
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

    pub fn get_selected_button_type(&self) -> MenuButtonType {
        if self.buttons.len() == 0 {
            return MenuButtonType::None;
        }

        self.buttons[self.selected_button_index].button_type
    }

    pub fn get_error_msg(&self) -> String {
        self.error_msg.clone()
    }
}

#[derive(Debug, PartialEq, Clone)]
pub struct MenuButton {
    text: String,
    selected: bool,
    button_type: MenuButtonType,
}

impl MenuButton {
    pub fn new(text: String, selected: bool, button_type: MenuButtonType) -> MenuButton {
        MenuButton { text: text, selected: selected, button_type: button_type }
    }

    pub fn get_text(&self) -> String {
        self.text.clone()
    }

    pub fn is_selected(&self) -> bool {
        self.selected
    }
}

// Buttons for installation method options
fn installation_method_buttons() -> Vec<MenuButton> {
    let install_buttons = vec![MenuButton::new(
        String::from(CONST_BUTTON_USB_INSTALL),
        true,
        MenuButtonType::USBInstall,
    )];

    install_buttons
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::MenuEvent::Navigate;

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
        assert_eq!(selected_type, MenuButtonType::USBInstall);
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
        assert_eq!(selected_type, MenuButtonType::USBInstall);
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
        assert_eq!(selected_type, MenuButtonType::USBInstall);
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
        assert_eq!(state, MenuState::SelectDisk);
        let heading = menu.get_heading();
        assert_eq!(heading, CONST_SELECT_DISK_HEADLINE);

        let selected_index = menu.selected_button_index;
        assert_eq!(selected_index, 0);
        let selected_type = menu.get_selected_button_type();
        assert_eq!(selected_type, MenuButtonType::Disk);

        Ok(())
    }
}
