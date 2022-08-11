## User App OTA Demo

This demo example is split into Protected app and User app and following are their details:

* Protected:
    - Searches for user partition in partition table and if present, tries to look for appropriate headers. If headers are found, it loads/maps the app in the user defined sections
    - Configures memory sections for lower privilege region (WORLD1) and grants access as configured by the developer
    - Spawns a separate low privilege task with user entry point
    - Executes normally as `main_task` with higher privilege after setting up the user space application
    - Accepts user app OTA URL through syscall interface and performs user app firmware upgrade.
* User:
    - Initializes WiFi in station mode. Connects to the AP provided by the user in [user_config.h](user_app/user_config.h)
    - Registers a console command to accept user app OTA URL

### Setting Up ESP-IDF

```
$ cd $IDF_PATH
$ git checkout v4.4.2
$ git submodule update --init --recursive
$ ./install.sh
$ source ./export.sh
```

#### Apply patch on IDF:

```
$ git apply -v <path/to/privilege-separation>/idf-patches/privilege-separation_support_v4.4.2.patch
```

### Hardware

- ESP32-C3 based development board

### Build and Flash

Build is separated in two parts: Protected app and User app.

- Protected app is a standalone app and is only dependent on ESP-IDF
- User app relies on certain information and libraries derived from the protected build

```
$ idf.py build
$ idf.py flash monitor
```

### Initiating OTA updates

User app accepts OTA URL using `user-ota <url>` command through console.

### Local HTTP Server
 - Enter the directory containing build artifact/s of project, that will be hosted by HTTP server, e.g. `cd build`
 - To start the python server, run command `python -m http.server <port>`
 - This directory should contain the firmware (e.g. user_app.bin) to be used in the update process. This can be any ESP Privilege-Separation application (excluding ESP-RainMaker application).
 - **NOTE:** We are using plain HTTP server in this example for demonstration purpose. It is recommended to use a HTTPS connection for performing OTA upgrades.
 - **NOTE:** Protected app can connect to public HTTPS servers without any additional configuration.
