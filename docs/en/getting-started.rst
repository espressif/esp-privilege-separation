Getting Started
===============

This document is intended to help you understand the structure and approach for developing applications under
Privilege Separation framework.

Directory structure
-------------------

ESP Privilege Separation project has a slightly different directory structure than a normal ESP-IDF application (refer to `ESP-IDF directory structure <https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32c3/api-guides/build-system.html#example-project>`_).

An example project under privilege separation philosophy looks like this::

    my_project
    |-- build
    |-- CMakeLists.txt
    |-- partitions.csv
    |-- protected_app
    |   `-- main
    |       |-- CMakeLists.txt
    |       `-- protected_main.c
    |-- sdkconfig
    `-- user_app
        |-- CMakeLists.txt
        |-- main
        |   |-- CMakeLists.txt
        |   `-- user_code.c
        `-- user_config.h

Privilege separation project contains the following elements:

- A top level CMakeLists.txt similar to the one in ESP-IDF main project. It is the primary file which the CMake uses to learn
  how to build the project. Apart from the ESP-IDF components, we add all the components of ``esp-privilege-separation`` and
  ``protected_app`` by setting the ``EXTRA_COMPONENT_DIRS`` variable. This is required because we do not have a ``main`` component
  which was automatically added by the ESP-IDF build system.
- ``sdkconfig`` project configuration file. This file is created and holds the configuration of the entire project. It contains the
  configuration for protected as well as the user app.
- ``protected_app`` is protected space application. It contains ``main`` component of the protected application.
  Code contained inside this directory executes under the protected space.
- ``user_app`` is the user space application. It is built as an external project to the protected app.
  It is similar to an independent ESP-IDF project and contains a top level CMakeLists.txt to build the ``user_app``.
  Code contained inside this directory executes under the user space.
- ``partitions.csv`` file specifying the partitions and the space reserved for protected app and user app.
  User app needs to have the partition name as ``user_app`` with type ``app`` and sub-type ``0xFF``.
- ``build`` directory which is created when the project is built and it contains the build output of the project.
  It contains the ELFs and BINs of protected and user apps.

Writing a sample application
----------------------------

The traditional ESP-IDF application is a single monolithic application. In this proposed model, we try to split the application
in two separate binaries.

Let's walkthrough one of the sample application:

Protected application
~~~~~~~~~~~~~~~~~~~~~

Protected application is equivalent to any ESP-IDF application. You can use any component from ESP-IDF or otherwise, as it is.
``esp_priv_access`` component APIs needs to be used for configuring and booting the user application.

::

    IRAM_ATTR void user_app_exception_handler(void *arg)
    {
        // Perform actions when user app exception happens
    }

    void app_main()
    {
        esp_err_t ret;

        ret = esp_priv_access_init(user_app_exception_handler);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize PA %d\n", ret);
        }

        esp_priv_access_set_periph_perm(PA_GPIO, PA_WORLD_1, PA_PERM_ALL);

        esp_ws2812_device_register("/dev/ws2812");

        ret = esp_priv_access_user_boot();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to boot user app %d\n", ret);
        }

        for (int i = 0; ; i++) {
           ets_printf("Hello from protected environment\n");
           vTaskDelay(500);
        }
    }


``app_main`` is the function that will be called by the 2nd stage bootloader. This is the usual entry point for traditional
ESP-IDF applications. In this model, ``app_main`` will be the entry point of the protected application.
Any code that goes into this directory is executed under protected space and has access to the entire system.
``esp_priv_access_init`` configures the memory region and sets up the permissions for user app.
``user_app_exception_handler`` is the handler that will be invoked in case there is any exception in user space code.
``esp_priv_access_user_boot`` finds the user app in flash and if found, spawns a new task that starts executing it.

Apart from init and booting user app, protected app developer can also give explicit permission to some of the peripherals.
::

    esp_priv_access_set_periph_perm(PA_GPIO, PA_WORLD_1, PA_PERM_ALL);

With this line, it has granted the user application access to GPIO peripheral. Any user application that is executed with this
protected app can access the GPIO registers. For more details, refer :doc:`ESP Priv Access<api-reference/esp_priv_access>`.

::

    esp_ws2812_device_register("/dev/ws2812");

This is a sample driver that we have included in the components to demonstrate how to write a driver for a specific device and
how it can be registered so that user application is able to use it. The driver is implemented in protected space
but the user can use it through the VFS layer (open, read, write, close). Check the implementation at :component_file:`drivers/ws2812/ws2812.c`.

For more details about the driver development, please refer :ref:`Driver development<driver_devel>`.


User application
~~~~~~~~~~~~~~~~

User application is supposed to contain the non-critical, business logic of the application. The idea is that even if there is any
misbehavior in the user application, the system (i.e. protected app) isn't affected by it. This allows us to have a robust and
resilient system.

What a user application can do depends on the protected app and its configuration. With the protected app which we have developed
above, user application can access GPIO registers directly as well as control the WS2812 LED and so we will write a small application
that toggles a GPIO directly and controls WS2812 LED through VFS system calls.

::

    void blink_task()
    {
        /* WS2812 LED expects data in multiple of 3. 3 bytes for 1 LED
         * The data format is {R, G, B}, with intensity ranging from 0 - 255.
         * 0 being dimmest (off) and 255 being the brightest
         */
        uint8_t data_on[3] = {0, 8, 8};
        uint8_t data_off[3] = {0, 0, 0};

        ws2812_dev_conf_t dev_cnf = {
            .channel = 0,
            .gpio_num = WS2812_GPIO,
            .led_cnt = 1
        };

        int ws2812_fd = open("/dev/ws2812/0", O_WRONLY);

        ioctl(ws2812_fd, WS2812_INIT, &dev_cnf);

        while (1) {
            gpio_ll_set_level(&GPIO, BLINK_GPIO, 1);
            write(ws2812_fd, data_on, 3);
            vTaskDelay(100);

            gpio_ll_set_level(&GPIO, BLINK_GPIO, 0);
            write(ws2812_fd, data_off, 3);
            vTaskDelay(100);
        }
    }

    void user_main()
    {
        gpio_config_t io_conf;
        io_conf.pin_bit_mask = (1 << BLINK_GPIO);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
        io_conf.pull_down_en = 0;
        io_conf.pull_up_en = 0;
        gpio_config(&io_conf);

        if (xTaskCreate(blink_task, "Blink task", 4096, NULL, 1, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Task Creation failed");
        }
    }


``user_main`` is the entry point of the user application. We configure the GPIO just like we would do in a traditional ESP-IDF
blink example. We then create a task ``blink_task`` that handles the toggling of the GPIO as well as the WS2812 LED.
As the protected application has already granted access to GPIO registers, we can directly write to them
and toggle the GPIO state instead of going through the system call approach. This definitely saves some extra
CPU overhead. For toggling WS2812 LED, we use the VFS system calls to configure and operate the LED.

As you can see, most of the APIs remain consistent between protected and user app and choosing the appropriate definition is
handled by the build system (For more details, refer :ref:`Translation to system call<trans_syscall>`). There are
some exceptions to this, certain API prototypes cannot be kept consistent as it may require some additional user context. Such APIs
can be found under ``esp_syscall`` component.


Memory allocation
~~~~~~~~~~~~~~~~~

Memory is divided between protected and user app based on the Kconfig options set.

Using the provided Kconfig options under "Memory allocation" menu in "Privilege Separation" section,
you can:

- Reserve IRAM memory (code) for user application.
- Reserve DRAM space (data + bss + heap) for either protected app or user app.
  The size specified will be the DRAM size for the chosen application,
  rest of the memory will be allocated to the other app.

For this above example, we have kept the default memory allocation policy with default sizes
as that can meet this application requirement.