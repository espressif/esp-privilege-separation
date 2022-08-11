## ESP Rainmaker Demo

This demo example is split into Protected app and User app and following are their details:

* Protected:
    - Searches for user partition in partition table and if present, tries to look for appropriate headers. If headers are found, it loads/maps the app in the user defined sections
    - Configures memory sections for lower privilege region (WORLD1) and grants access as configured by the developer
    - Spawns a separate low privilege task with user entry point
    - Executes normally as `main_task` with higher privilege after setting up the user space application
    - Exposes system calls for ESP-Rainmaker framework
* User:
    - Initializes ESP Rainmaker service in the protected application
    - Creates a Rainmaker switch device
    - Supports state change as per events from Rainmaker cloud and from GPIO

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

### Setting Up ESP-Rainmaker

```
$ git clone --recursive https://github.com/espressif/esp-rainmaker
$ git checkout 00bcf4c0c30d96b8954660fb396ba313fb6c886f
$ export RMAKER_PATH=<path-to-esp-rainmaker-repository>
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

### Changes in the upstream Rainmaker example to support ESP-Privilege-Separation

- System call layer uses the data stored in device context to execute user-space write and read callbacks. Set the function pointers to app_write_cb and app_read_cb in rmaker_dev_priv_data_t structure. Save the pointer to this structure in device context by passing the pointer to esp_rmaker_device_create.
- esp_rmaker_param_get_name and esp_rmaker_device_get_name APIs do not work in the user app. Use usr_esp_rmaker_param_get_name and usr_esp_rmaker_device_get_name instead.
