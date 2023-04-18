use crate::{
    config,
    error::Error,
};

use std::{fs::{rename, copy, remove_file, DirEntry, remove_dir}, path::{Path, PathBuf}};

use nix::{
    unistd::mkdir,
    sys::{stat, statfs::statfs},
    errno,
};

type Cleaner = std::thread::JoinHandle<Result<(), Error>>;


enum Threshold {
    Free {
        size: u64
    },
    Used {
        size: u64
    },
}

impl Clone for Threshold {
    fn clone(&self) -> Self {
        match self {
            Self::Free { size } => Self::Free { size: *size },
            Self::Used { size } => Self::Used { size: *size },
        }
    }
}


fn size_from_human_readable(human_readable: &String) -> u64 {
    // let mut size = human_readable.parse().expect("Failed to parse size");
    let mut multiply = 1;
    let mut id = 0;
    for char in human_readable.chars() {
        match char {
            '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9'  => {
                id += 1;
            },
            'k' | 'K' => {
                multiply = 0x400;
                break
            },
            'm' | 'M' => {
                multiply = 0x100000;
                break;
            },
            'g' | 'G' => {
                multiply = 0x40000000;
                break;
            },
            't' | 'T' => {
                multiply = 0x10000000000;
                break;
            }
            _ => panic!("Invalid character in size"),
        }
    }
    let base = &human_readable[0..id];
    let base: u64 = base.parse().expect("Failec to parse size");
    base * multiply
}

// fn size_from_threshold(threshold: &config::Threshold) -> u64 {
//     0
// }

// fn sizes_from_thresholds(thresholds: &config::Thresholds) -> (u64, u64) {
//     (size_from_threshold(&thresholds.begin), size_from_threshold(&thresholds.end))
// }

impl From<&config::Threshold> for Threshold {
    fn from(config: &config::Threshold) -> Self {
        if let Some(human_readable) = &config.free {
            return Self::Free { 
                size: size_from_human_readable(human_readable)
            };
        };
        if let Some(human_readable) = &config.used {
            return Self::Used { 
                size: size_from_human_readable(human_readable)
            };
        };
        panic!("Neither free or used not defined");
    }
}

// struct Thresholds {
//     begin: Threshold,
//     end: Threshold,
// }

// impl From<&config::Thresholds> for Thresholds {
//     fn from(config: &config::Thresholds) -> Self {
//         Self {
//             begin: Threshold::from(&config.begin),
//             end: Threshold::from(&config.end),
//         }
//     }
// }

pub struct Storage {
    name: String,
    cleaner: Option<Cleaner>,
    clean_begin: Threshold,
    clean_ongoing: bool,
    clean_end: Threshold,
    is_half_duplex: bool,
}

pub(crate) fn ensure_parent_folder(path: &str) -> Result<(), errno::Errno> {
    let id = path.rfind('/').expect("Failed to find seperator");
    let parent = &path[0..id];
    if let Err(e) = mkdir(parent, stat::Mode::S_IRWXU) {
        match e {
            errno::Errno::ENOENT => {
                if let Err(e) = ensure_parent_folder(parent) {
                    return Err(e);
                }
                if let Err(e) = mkdir(parent, stat::Mode::S_IRWXU) {
                    return Err(e);
                }
            },
            errno::Errno::EEXIST => (),
            _ => return Err(e),
        }
    }
    Ok(())
}

fn move_file(source: &Path, destination: &Path) -> Result<(), Error> {
    match destination.to_str() {
        Some(destination) => {
            match ensure_parent_folder(destination) {
                Ok(_) => (),
                Err(_) => return Err(Error::FailedIO),
            }
        },
        None => return Err(Error::FailedIO),
    };
    match rename(source, destination) {
        Err(e) => match e.raw_os_error() {
            Some(errno) => {
                if errno == errno::Errno::EXDEV as i32 {
                    if let Ok(_) = copy(source, destination) {
                        if let Ok(_) = remove_file(source) {
                            return Ok(());
                        };
                    };
                }
            }
            None => (),
        }
        Ok(_) => return Ok(()),
    }
    Err(Error::FailedIO)
}

fn find_oldest_file_in_folder(folder: &Path) -> Option<DirEntry> {
    let mut oldest: Option<DirEntry> = None;
    if let Ok(rd) = std::fs::read_dir(folder) {
        let mut entries_count = 0;
        for entry in rd {
            if let Ok(entry) = entry {
                entries_count += 1;
                if let Ok(file_type) = entry.file_type() {
                    if let Some(entry) = {
                        if file_type.is_dir() {
                            if let Some(entry) = find_oldest_file_in_folder(&entry.path()) {
                                Some(entry)
                            } else {
                                None
                            }
                        } else if file_type.is_file() {
                            Some(entry)
                        } else {
                            None
                        }
                    } {
                        match oldest {
                            Some(ref oldest_entry) => {
                                if let Ok(metadata) = oldest_entry.metadata() {
                                    if let Ok(modified_oldest) = metadata.modified() {
                                        if let Ok(metadata) = entry.metadata() {
                                            if let Ok(modified_entry) =  metadata.modified() {
                                                if modified_entry < modified_oldest {
                                                    oldest = Some(entry);
                                                }
                                            }
                                        }

                                    }
                                }
                            },
                            None => oldest = Some(entry),
                        }

                    }
                }
            }
        }
        if entries_count == 0 {
            match remove_dir(folder) {
                Ok(_) => println!("Removed an empty folder"),
                Err(_) => println!("Failed to remove an empty folder"),
            }
        }
    }
    if let None = oldest {
        println!("Warning, failed to find oldest file in {}", folder.to_str().unwrap());
    }
    return oldest
}

fn clean_folder(folder: &str, target: Option<&str>) -> Result<(), Error>{
    if let Some(entry) = find_oldest_file_in_folder(&PathBuf::from(folder)) {
        let path = entry.path();
        if let Some(target) = target {
            let mut target = PathBuf::from(target);
            if let Ok(suffix) = path.strip_prefix(folder) {
                target.push(suffix);
                if let Err(_) = move_file(&path, &target) {
                    return Err(Error::FailedIO);
                }
            } else {
                return Err(Error::FailedIO);
            }
        } else {
            if let Err(_) = remove_file(path) {
                return Err(Error::FailedIO);
            }
        }
    };
    Ok(())
}

fn clean_folder_remove(folder: &str) -> Result<(), Error> {
    clean_folder(folder, None)
}

fn clean_folder_remove_until(folder: &str, until: Threshold) -> Result<(), Error> {
    loop {
        if let Err(e) = clean_folder_remove(folder) {
            return Err(e);
        }
        if let Ok(st) = statfs(folder) {
            let free_size = st.block_size() as u64 * st.blocks_available();
            let used_size = st.block_size() as u64 * st.blocks() - free_size;
            match &until {
                Threshold::Free { size } => {
                    if free_size >= *size {
                        break
                    }
                },
                Threshold::Used { size } => {
                    if used_size <= *size {
                        break
                    }
                },
            };
        }
    }
    Ok(())

}

fn clean_folder_move(folder: &str, target: &str) -> Result<(), Error> {
    clean_folder(folder, Some(target))
}

impl Storage {
    pub(crate) fn from_config(config: &config::Config) -> Vec<Self> {
        let mut storages = vec![];
        for storage in config.storages.iter() {
            let mut is_half_duplex = false;
            for flag in storage.flags.iter() {
                match flag.as_str() {
                    "half_duplex" => {
                        println!("Storage {} is half-duplex", storage.name);
                        is_half_duplex = true;
                    },
                    _ => (),
                }
            }
            storages.push(Storage {
                name: storage.name.clone(),
                cleaner: None,
                clean_begin: Threshold::from(&storage.thresholds.begin),
                clean_ongoing: false,
                clean_end: Threshold::from(&storage.thresholds.end),
                is_half_duplex,
            });
        }
        if storages.is_empty() {
            panic!("No storage defined");
        }
        storages
    }

    fn need_clean(&self) -> bool {
        if let Ok(st) = statfs(self.name.as_str()) {
            let free_size = st.block_size() as u64 * st.blocks_available();
            let used_size = st.block_size() as u64 * st.blocks() - free_size;
            if self.clean_ongoing {
                match self.clean_end {
                    Threshold::Free { size } => {
                        if free_size <= size {
                            return true;
                        }
                    },
                    Threshold::Used { size } => {
                        if used_size >= size {
                            return true;
                        }
                    }
                }
            } else {
                match self.clean_begin {
                    Threshold::Free { size } => {
                        if free_size <= size {
                            return true;
                        }
                    },
                    Threshold::Used { size } => {
                        if used_size >= size {
                            return true;
                        }
                    },
                }
            }
        }
        false
    }

    fn accept_write(storage: Option<&Self>) -> bool {
        /* is a storage device, need to take care */
        if let Some(storage) = storage {
            /* is a half duplex device, need to take care */
            if storage.is_half_duplex {
                /* is being cleaned, need to take care */
                if let Some(_) = &storage.cleaner {
                    return false
                }
            }
        }
        true
    }

    pub(crate) fn ensure_space(storages: &mut Vec<Self>) {
        let mut next_storage = None;
        for storage in storages.iter_mut().rev() {
            if let Some(cleaner) = &storage.cleaner {
                if cleaner.is_finished() {
                    let cleaner = std::mem::replace(&mut storage.cleaner, None);
                    if let Some(cleaner) = cleaner {
                        match cleaner.join().expect("Failed to join") {
                            Ok(_) => (),
                            Err(e) => {
                                println!("A cleaner failed with {:?}", e);
                            }
                        }
                    } else {
                        panic!("Failed to get join handle of cleaner");
                    }
                }
            }
            if let None = &storage.cleaner {
                if storage.need_clean() {
                    storage.clean_ongoing = true;
                    if Storage::accept_write(next_storage) {
                        let source = storage.name.clone();
                        match next_storage {
                            Some(next_storage) => {
                                let target = next_storage.name.clone();
                                storage.cleaner = Some(std::thread::spawn(move || clean_folder_move(&source, &target)));
                            },
                            None => {
                                let threshold = storage.clean_end.clone();
                                storage.cleaner = Some(std::thread::spawn(move || clean_folder_remove_until(&source, threshold)));
                            },
                        };
                    }
                } else {
                    storage.clean_ongoing = false;
                }
            }
            next_storage = Some(storage);
        }
    }
    
}