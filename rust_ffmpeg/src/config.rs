use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub struct Threshold {
    pub free: String,
    pub used: String,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub struct Thresholds {
    pub begin: Threshold,
    pub end: Threshold,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub struct Storage {
    pub name: String,
    pub thresholds: Thresholds,
    pub flags: Vec<String>,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub struct Camera {
    pub name: String,
    pub url: String,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub struct Time {
    pub naming: String,
    pub segment: u32,
    pub stop_delay: u32,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub struct Config {
    pub storages: Vec<Storage>,
    pub cameras: Vec<Camera>,
    pub time: Time,
    pub suffix: String,
}

pub fn read() -> Config {
    let config: Config = serde_yaml::from_reader(
        std::fs::File::open("nvr.yaml")
            .expect("Failed to open config file, make sure it's stored as nvr.yaml"))
                .expect("Failed to parse config file, make sure it's valid YAML");
    if config.cameras.is_empty() {
        panic!("No camera was defined");
    }
    if config.storages.is_empty() {
        panic!("No storage was defined");
    }
    return config;
}