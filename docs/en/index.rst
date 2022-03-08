ESP Privilege Separation Programming Guide
==========================================

ESP Privilege Separation is an approach to separate out traditional monolithic RTOS firmware into 2 independent executables, a protected_app and a user_app, with a clearly defined system-call interface between them. The protected app executes in a higher privilege mode, with full access to the entire system memory and all peripherals whereas user application has restricted memory and peripheral access (as defined and granted by the protected app).

.. figure:: ../img/overview.png
    :align: center
    :alt: Overview
    :figclass: align-center

    Framework overview

Features
--------

- Two separate independent firmware binaries generated at build time
- Protected application contains operating system, networking stack, device drivers and hence does bulk of the heavy lifting. Developers can add any custom modules to the protected application as per their needs
- User application is very thin. It typically implements the business logic, and it avails of the various services by primarily using the system call based interface exposed by the protected app
- The protected app can configure the entire memory space and peripheral access at run-time, based on the requirement
- Any exception within the user application does not hamper the functionality of the protected app
- The user-kernel boundary is configurable. Developers can move certain modules within or outside the kernel space easily

Future Work
-----------

The ESP Privilege Separation project is under active development. There are a wide range of problems that are unsolved
yet, and are quite interesting to explore. We will be addressing these in the days to come. With this release, our goal
is to enable a minimal set of functionality that can achieve the user-kernel privilege separation with minimal memory
bloat for user applications, at the same time, maintaining simplicity and backward compatibility for the IDF APIs. If
you have a specific requirement that you believe fits well in this framework, or if solving such problems excites you,
we'd love to talk to you for collaboration.
  
.. toctree::
   :hidden:

   Getting started <getting-started>
   Techincal Details <technical-details/index>
   API Reference <api-reference/index>
