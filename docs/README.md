# Documentation Source Folder

This folder contains source files of **ESP Privilege Separation documentation**.

The sources do not render well in GitHub and some information is not visible at all.

Use actual documentation generated within about 20 minutes on each commit:

# Building Documentation

The documentation is built using the python package esp-docs, which can be installed by running `pip install esp-docs`. Running `build-docs --help` will give a summary of available options. For more information see the esp-docs documentation at https://github.com/espressif/esp-docs/blob/master/README.md

To build docs for ESP32-C3, run command `build-docs -t esp32c3 -l en`

In case you face an error relating to `cairo` or `libcairo-2` library not found, please install package through the corresponding package manager
