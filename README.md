# ESP Privilege Separation

Aim of this offering is to showcase an ability to separate out traditional **monolithic** RTOS firmware into 2 independent executable, `protected_app` and `user_app` with clearly defined `system-call` interface between them. Protected app executes in higher privilege mode, with full access to entire system memory and all peripherals whereas user application has restricted memory and peripheral access (as defined and granted by protected app).

### Some additional highlights:
 - Privilege separation between user and protected app is achieved using WORLD controller and Permission controller peripherals
 - Only 2 privilege levels WORLD0 and WORLD1 are available, more levels can be added in future chips
 - Entire memory space and peripheral access is run-time configurable. Memory can be split between privileged and non-privileged environments on the fly
 - Protected app contains operating system, networking stack, device drivers and hence doing bulk of heavy lifting
 - User application is very thin, primarily using system call based interface exposed by protected app to interact and avail various services
 - Privilege Separation allows:
    - Easy maintenance, e.g. individual over-the-air updates for either protected or user application
    - Secure and robust system behavior, since user application has only required access to system

## Examples

For building and flashing examples please refer to corresponding documentation per below:

### [Blink](examples/blink)

This example demonstrates simple controlled peripheral access from user application running in lower privilege mode. Protected app grants only GPIO peripheral access to user application, and then user application toggles GPIO from task running in its environment.

### [HTTP Request](examples/http_request)

This example demonstrates HTTP request example, where user application periodically queries a HTTP URL and retrieves data from it. HTTP client along with all networking dependencies lies in protected space, and user application uses read/write kind of system call interface to retrieve/send data.

### [HTTPS Request](examples/https_request)

This example demonstrates HTTPS request example, where user application queries a HTTPS URL and retrieves data from it. TLS operations along with all networking dependencies lies in protected space, and user application uses TLS read/write system call interface to retrieve/send data.

#### Hardware
- ESP32-C3 based development board

## Technical Details

### Architecture

![firmware_architecture](docs/img/firmware_architecture.png)

The firmware architecture comprises of 2 different executables:

**Protected**:
- Trusted application that requires complete system access and isolation from user applications
- Consists of crucial IDF components like FreeRTOS, WiFi, Networking stack
- Exposes system calls to the user space which allows user application to delegate functions to protected space as it requires access to critical features (OS, networking)

**User**:
- Consists of system call interface that can be used to request protected app to execute a service in protected space
- Untrusted application logic in order to safeguard from hampering the entire system


### Bootup Flow

- The [boot-up process](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32c3/api-guides/startup.html) of the IDF application stays the same. `app_main` is essentially the entry point of our protected app
- Protected app is responsible for setting permissions, configuring memory regions and loading the user application from flash. It also exposes a set of system calls that user can invoke in order to request certain resources (creating a task, registering ISR, etc)
- User app depends upon the permissions granted by the protected app and has to work within those boundaries. If the user app wants to perform certain privileged operation then it needs to invoke the system calls provided by the protected app.

**For more implementation details, refer [this](/docs/implementation-details.md)**
