# ESP Privilege Separation (Beta)

ESP Privilege Separation is an approach to separate out traditional **monolithic** RTOS firmware into 2 independent executables, `protected_app` and `user_app`, with different privilege levels and a clearly defined `system-call` interface between them. Protected app executes in higher privilege mode, with full access to entire system memory and all peripherals whereas user application has restricted memory and peripheral access (as defined and granted by protected app).

### Features

- Two separate independent firmware binaries generated at build time
- Protected application contains operating system, networking stack, device drivers and hence doing bulk of heavy lifting. Developers can add custom routines as per their needs
- User application is very thin, primarily using system call based interface exposed by protected app to interact and avail various services
- Entire memory space and peripheral access is run-time configurable. Memory can be split between privileged and non-privileged environments on the fly
- Exceptions within the user application do not hamper the functionality of the protected app
- Enables secure and robust system behavior, as user application has only required access to system

### Overview

![overview](docs/img/overview.png)

The entire firmware comprises of 2 different executables:

#### Protected

- Trusted application that requires complete system access and isolation from user applications.
- Consists of crucial ESP-IDF components like FreeRTOS, WiFi, Networking stack.
- Exposes system calls to the user space which allows user application to delegate functions to protected space as it requires access to critical features (OS, networking).

#### User

- Consists of system call interface that can be used to request protected app to execute a service in protected space.
- Untrusted application logic in order to safeguard from hampering the entire system.

## Getting Started

To get started, please try out the [examples](examples). Each example has README with all setup instructions.

#### Supported SoCs
- ESP32-C3 based development board
