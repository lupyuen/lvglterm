![LVGL Terminal for PinePhone on Apache NuttX RTOS](https://lupyuen.github.io/images/lvgl2-terminal3.jpg)

# LVGL Terminal for PinePhone on Apache NuttX RTOS

LVGL Terminal `lvglterm` is a Touchscreen App for PinePhone that lets us run commands in the NuttX NSH Shell...

-   [Watch the Demo on YouTube](https://www.youtube.com/watch?v=WdiXaMK8cNw)

To build LVGL Terminal...

1.  Add `lvglterm` to our NuttX Project...

    ```bash
    pushd nuttx/apps/examples
    git submodule add https://github.com/lupyuen/lvglterm
    popd
    ```

1.  Configure our NuttX Project...

    ```bash
    pushd nuttx/nuttx
    make distclean
    tools/configure.sh pinephone:lvgl
    make menuconfig
    ```

1.  In "RTOS Features > Tasks and Scheduling"

    Set "Application entry point" to `lvglterm_main`

    Set "Application entry name" to `lvglterm_main`

1.  In "Device Drivers"

    Enable "FIFO and named pipe drivers"

1.  In "Application Configuration > Examples"

    Enable "LVGL Terminal"

1.  In "Application Configuration > NSH Library"

    Disable "Have architecture-specific initialization"

1.  In "Application Configuration > Graphics Support > Light and Versatile Graphic Library (LVGL) > LVGL configuration > Font usage > Enable built-in fonts"

    Enable "UNSCII 16"

1.  Save configuration and exit `menuconfig`

1.  Build NuttX...

    ```bash
    make
    ```
