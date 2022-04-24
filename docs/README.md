# Documentation Source Folder

This folder contains source files of **ESP Privilege Separation documentation**.

The sources do not render well in GitHub and some information is not visible at all.

Use actual documentation generated within about 20 minutes on each commit:

# Building Documentation

* Install `doxygen` on your platform.
* Install the python dependencies `python -m pip install -r requirements.txt`.
* Build docs for ESP32-C3 by running `build-docs -t esp32c3 -l en`.

In case you face an error relating to `cairo` or `libcairo-2` library not found, please install this package through the corresponding package manager.
