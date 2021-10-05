Privilege Separation Programming Guide
======================================
Aim of this offering is to showcase an ability to separate out traditional monolithic RTOS firmware into 2 independent executable, protected_app and user_app with clearly defined system-call interface between them. Protected app executes in higher privilege mode, with full access to entire system memory and all peripherals whereas user application has restricted memory and peripheral access (as defined and granted by protected app).

Some additional highlights
##########################

* Privilege separation between user and protected app is achieved using WORLD controller and Permission controller peripherals
* Only 2 privilege levels WORLD0 and WORLD1 are available, more levels can be added in future chips
* Entire memory space and peripheral access is run-time configurable. Memory can be split between privileged and non-privileged environments on the fly
* Protected app contains operating system, networking stack, device drivers and hence doing bulk of heavy lifting
* User application is very thin, primarily using system call based interface exposed by protected app to interact and avail various services
* Privilege Separation allows:

    * Easy maintenance, e.g. individual over-the-air updates for either protected or user application
    * Secure and robust system behavior, since user application has only required access to system

.. toctree::
   :maxdepth: 2

   Technical Details <technical-details/index>
   API Reference <api-reference/index>
