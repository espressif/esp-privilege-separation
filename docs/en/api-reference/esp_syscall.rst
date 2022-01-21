ESP SYSCALL
===========

Overview
--------

This component consists of system call implementation of ESP-IDF components like FreeRTOS, LwIP, etc.
It also has scripts to automatically translate APIs to their system calls based on the build type (user or protected).
There are certain APIs which cannot be kept consistent with their ESP-IDF equivalent functions and such "custom" system calls are also
implemented in this component.

API reference
-------------

.. include-build-file:: inc/syscall_wrappers.inc

