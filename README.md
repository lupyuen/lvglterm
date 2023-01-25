![LVGL Terminal for PinePhone on Apache NuttX RTOS](https://lupyuen.github.io/images/lvgl2-terminal3.jpg)

# LVGL Terminal for PinePhone on Apache NuttX RTOS

TODO

```bash
pushd nuttx/apps/examples
git submodule add https://github.com/lupyuen/lvglterm
popd

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

```bash
make
```
