use nix::{
    unistd::mkdir,
    sys::stat,
    errno,
};

pub fn ensure_parent_folder(path: &str) -> Result<(), errno::Errno> {
    let id = path.rfind('/').expect("Failed to find seperator");
    let parent = &path[0..id];
    if let Err(e) = mkdir(parent, stat::Mode::S_IRWXU) {
        dbg!(e);
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