# Network Video Recorder Implementations in various langauges

## C
Codes are under `c/`
### Compile
```
make c
```
Output binary is `c/nvr`
### Run
```
./nvr --storage [storage definition] (--storage [storage definition] (--storage [storage definition] (...)))
      --camera [camera definition] (--camera [camera definition] (--camera [camera definition] (...)))
      --help
      --version

  - [storage deinition]: [path]:[thresholds]
    - [path]: folder name or path, relative or absolute both fine
    - [thresholds]: [from]:[to]
      - [from]: when free space <= this percent, triggers cleaning
      - [to]: when free space >= this percent, stops cleaning
  - [camera definition]: [name]:[url]
    - [name]: 
    - [url]: a valid input url for ffmpeg
```
Example:
```
./nvr --camera rooftop:rtsp://127.0.0.1:8554/rooftop --camera road:rtsp://127.0.0.1:8554/road --camera garden:rtsp://127.0.0.1:8554/garden --storage hot:10:90 --storage warm:5:10 --storage cold:1:5
```

## C++
Codes are under `cpp`
### Compile
```
make cpp
```
Output binary is `cpp/nvr`

## Python
Codes are under `python`