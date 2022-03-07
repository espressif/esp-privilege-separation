Architecture
============

Overview
--------

.. image:: ../../img/firmware_architecture.png

The firmware architecture comprises of 2 different executables:

Protected
~~~~~~~~~~

- Trusted application that has the complete system access and is isolated from user applications.
- Consists of crucial ESP-IDF components like FreeRTOS, WiFi, Networking stack.
- Exposes system calls to the user space which allows user application to delegate functions to protected space as it requires access to critical features (OS, networking).


User
~~~~~

- Consists of system call interface that can be used to request protected app to execute a service in protected space.
  - A `vTaskDelay()` from the user-space will internally be redirected to `usr_vTaskDelay()` which actually executes the system call.
- Untrusted application logic in order to safeguard from hampering the entire system.


Bootup Flow
-----------

- The `boot-up process <https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32c3/api-guides/startup.html>`_ of the ESP-IDF application stays the same. ``app_main`` is essentially the entry point of our protected app.
- Protected app is responsible for setting permissions, configuring memory regions and loading the user application from flash. It also exposes a set of system calls that user can invoke in order to request certain resources (creating a task, accessing peripherals, etc).
- User app depends upon the permissions granted by the protected app and has to work within those boundaries. If the user app wants to perform certain privileged operation then it needs to invoke the system calls provided by the protected app.
