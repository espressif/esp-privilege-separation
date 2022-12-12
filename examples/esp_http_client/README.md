## ESP HTTP Client Demo

This demo example is split into Protected app and User app and following are their details:

* Protected:
    - Searches for user partition in partition table and if present, tries to look for appropriate headers. If headers are found, it loads/maps the app in the user defined sections
    - Configures memory sections for lower privilege region (WORLD1) and grants access as configured by the developer
    - Spawns a separate low privilege task with user entry point
    - Executes normally as `main_task` with higher privilege after setting up the user space application
* User:
    - Initializes WiFi in station mode. Connects to the AP provided by the user in [user_config.h](user_app/user_config.h)
    - Spawns a task which sends an HTTP/S requests to a website and prints out the response on console

### Setting Up ESP-IDF

```
$ cd $IDF_PATH
$ git checkout v4.4.3
$ git submodule update --init --recursive
$ ./install.sh
$ source ./export.sh
```

#### Apply patch on IDF:

```
$ git apply -v <path/to/privilege-separation>/idf-patches/privilege-separation_support_v4.4.3.patch
```

### Hardware

- ESP32-C3 based development board
- ESP32-S3 based development board

### Build and Flash

Build is separated in two parts: Protected app and User app.

- Protected app is a standalone app and is only dependent on ESP-IDF
- User app relies on certain information and libraries derived from the protected build

```
$ idf.py set-target { esp32c3 | esp32s3 }
$ idf.py build
$ idf.py flash monitor
```
