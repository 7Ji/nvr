import dataclasses
import asyncio
import pathlib
import itertools
import shutil

@dataclasses.dataclass
class Camera:
    url: str
    name: str
    strftime: str = dataclasses.field(init=False)

    def __post_init__(self):
        self.dir = pathlib.Path(self.name)
        self.dir.mkdir(
            mode = 0o744, 
            parents = True,
            exist_ok = True
        )
        self.strftime = f"{self.name}/%y%m%d/%H%M%S.mkv"

    async def get_recorder(self):
        return await asyncio.subprocess.create_subprocess_exec(
            "ffmpeg",
            # Input
                "-use_wallclock_as_timestamps", "1",
                "-rtsp_transport", "tcp",
                "-i", self.url,
            # Codec
                "-c", "copy",
            # Output
                "-f", "segment",
                "-segment_time", "3600",
                "-segment_atclocktime", "1",
                "-reset_timestamps", "1",
                "-strftime", "1",
                "-strftime_mkdir", "1",
                self.strftime,
            stdin=asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL
        )

    async def record(self):
        print(f"Record for {self.name} started")
        for i in itertools.count(1):
            print(f"Recorder {i} for {self.name} started")
            recorder = await self.get_recorder()
            r = await recorder.wait()
            print(f"Recorder {i} for {self.name} returns with {r}")

def _sort_files(file: pathlib.Path):
    return file.stat().st_mtime

host = "127.0.0.1"
cameras = (
    Camera(f"rtsp://{host}:8554/rooftop", "Rooftop"),
    Camera(f"rtsp://{host}:8554/road", "Road"),
    Camera(f"rtsp://{host}:8554/garden", "Garden")
)

def get_files(folders:tuple[pathlib.Path]):
    # (((file for file in subfolder.iterdir()) for subfolder in folder.iterdir()) for folder in folders)
    for folder in folders: # Name
        if folder.is_dir():
            for subfolder in folder.iterdir(): # YYYYMMDD
                if subfolder.is_dir():
                    for file in subfolder.iterdir(): # HHMMSS.mkv
                        if file.suffix == ".mkv":
                            yield file


async def cleaner():
    folders = tuple(pathlib.Path(camera.name) for camera in cameras)
    while True:
        usage = shutil.disk_usage(".")
        percent = usage.used / usage.total
        if percent > 0.95:
            print("Used too much (>95%), need to clean")
            files = sorted(get_files(folders), key=_sort_files)
            while percent > 0.9:
                try:
                    oldest = files.pop(0)
                except IndexError:
                    print("Files list is empty, re-sort the list")
                    files = sorted(get_files(folders), key=_sort_files)
                    try:
                        oldest = files.pop(0)
                    except IndexError as error:
                        print("Files list is still empty, this should not happen")
                        raise error
                finally:
                    oldest.unlink(True)
                    parent = oldest.parent
                    try:
                        next(parent.iterdir())
                    except StopIteration:
                        parent.rmdir()
                        files = sorted(get_files(folders), key=_sort_files)
                usage = shutil.disk_usage(".")
                percent = usage.used / usage.total
        await asyncio.sleep(1)


async def main():
    await asyncio.gather(
        *(camera.record() for camera in cameras),
        cleaner()
    )

if __name__ == '__main__':
    asyncio.run(main())