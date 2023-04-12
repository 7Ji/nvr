use serde::{Serialize, Deserialize};

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub(crate) struct Threshold {
    pub(crate) free: Option<String>,
    pub(crate) used: Option<String>,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub(crate) struct Thresholds {
    pub(crate) begin: Threshold,
    pub(crate) end: Threshold,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub(crate) struct Storage {
    pub(crate) name: String,
    pub(crate) thresholds: Thresholds,
    pub(crate) flags: Vec<String>,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub(crate) struct Camera {
    pub(crate) name: String,
    pub(crate) url: String,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub(crate) struct Time {
    pub(crate) naming: String,
    pub(crate) segment: u32,
    pub(crate) stop_delay: u32,
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub(crate) struct Config {
    pub(crate) storages: Vec<Storage>,
    pub(crate) cameras: Vec<Camera>,
    pub(crate) time: Time,
    pub(crate) suffix: String,
}

pub(crate) fn read() -> Config {
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