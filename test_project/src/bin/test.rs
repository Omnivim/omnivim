extern crate keyboard_query;
use std::thread;
use std::sync::mpsc;
use std::time;

use keyboard_query::{DeviceQuery, DeviceState};

fn main() {
    let device_state = DeviceState::new(); 
    let (tx, rx) = mpsc::channel();

    thread::spawn(move || {
        let device_state = DeviceState::new(); 
        loop {
            let keys = device_state.get_keys();
            if tx.send(keys).is_err() {
                break; // Stop if receiver is closed
            }
            thread::sleep(time::Duration::from_millis(1)); // Prevent CPU overuse
        }
    });

    loop {
        match rx.recv() {
            Ok(keys) => println!("{:?}", keys),
            Err(_) => break, // Exit loop if sender stops
        }
    }
}
