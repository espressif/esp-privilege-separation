# ESP Privilege Separation (Beta)

ESP Privilege Separation is an approach to separate out traditional **monolithic** RTOS firmware into 2 independent executables, `protected_app` and `user_app`, with different privilege levels and a clearly defined `system-call` interface between them. Protected app executes in a higher privilege mode, with full access to entire system memory and all peripherals; whereas the user application has a restricted memory and peripheral access (as defined and granted by the protected app).

### Features

- Two separate independent firmware binaries generated at build time
- Protected application contains operating system, networking stack, device drivers and hence does bulk of the heavy lifting. Developers can add any custom modules to the protected application as per their needs
- User application is very thin. It typically implements the business logic, and it avails of the various services by primarily using the system call based interface exposed by the protected app
- The protected app can configure the entire memory space and peripheral access at run-time, based on the requirement
- Any exception within the user application does not hamper the functionality of the protected app
- The user-kernel boundary is configurable. Developers can move certain modules within or outside the kernel space easily

### Overview

![overview](docs/img/overview.png)

The entire firmware comprises of 2 different executables:

#### Protected

- Trusted application that has the complete system access and is isolated from user applications.
- Consists of crucial ESP-IDF components like FreeRTOS, WiFi, Networking stack.
- Exposes system calls to the user space which allows user application to delegate functions to protected space as it requires access to critical features (OS, networking).

#### User

- Consists of system call interface that can be used to request protected app to execute a service in protected space.
  - A `vTaskDelay()` from the user-space will internally be redirected to `usr_vTaskDelay()` which actually executes the system call.
- Untrusted application logic in order to safeguard from hampering the entire system.

## Getting Started

To get started, please try out the [examples](examples). Each example has README with all setup instructions.

### Supported SoCs
- ESP32-C3 based development board

## Documentation

Please refer to the documentation for latest version at: https://docs.espressif.com/projects/esp-privilege-separation/en/latest/esp32c3/index.html. This documentation is built from the [docs directory](docs) of this repository.

## Future Work

The ESP Privilege Separation framework is under active development. There are a wide range of problems that are unsolved yet, and are quite interesting to explore. We will be addressing these in the days to come. With this release, our goal is to enable a minimal set of functionality that can achieve the user-kernel privilege separation with minimal memory bloat for user applications, at the same time, maintaining simplicity and backward compatibility for the ESP-IDF APIs.

If you have a specific requirement that you believe fits well in this framework, or if solving such problems excites you, we'd love to talk to you for collaboration.

