![LVGL Terminal for PinePhone on Apache NuttX RTOS](https://lupyuen.github.io/images/lvgl2-terminal3.jpg)

# LVGL Terminal for PinePhone on Apache NuttX RTOS

LVGL Terminal `lvglterm` is a Touchscreen App for PinePhone that lets us run commands in the NuttX NSH Shell. Read the article...

-   ["NuttX RTOS for PinePhone: LVGL Terminal for NSH Shell"](https://lupyuen.github.io/articles/terminal)

-   [Watch the Demo on YouTube](https://www.youtube.com/watch?v=WdiXaMK8cNw)

![Flow of LVGL Terminal for PinePhone on Apache NuttX RTOS](https://lupyuen.github.io/images/terminal-flow.jpg)

LVGL Terminal is now in Mainline NuttX. To build LVGL Terminal...

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

1.  In "Library Routines > Program Execution Options"

    Enable "exec[l|v] / posix_spawn() Support"

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

1.  If the NuttX Build fails with undefined reference to `lv_font_unscii_16`...

    ```text
    LD: nuttx
    aarch64-none-elf-ld: /private/tmp/nuttx/nuttx/staging/libapps.a(lvglterm.c.private.tmp.nuttx.apps.examples.lvglterm.o): in function `create_widgets':
    /private/tmp/nuttx/apps/examples/lvglterm/lvglterm.c:254: undefined reference to `lv_font_unscii_16'
    aarch64-none-elf-ld: /private/tmp/nuttx/apps/examples/lvglterm/lvglterm.c:254: undefined reference to `lv_font_unscii_16'
    make[1]: *** [Makefile:173: nuttx] Error 1
    make: *** [tools/Unix.mk:537: nuttx] Error 2
    ```

    Clear the NuttX Build Configuration...

    ```text
    make distclean
    tools/configure.sh pinephone:lvgl
    make menuconfig
    ```

    And redo the `menuconfig` steps from above, then rebuild.

    This happens because NuttX builds LVGL with the Makefile that's bundled with LVGL. And the Makefile Integration is a little wonky :-)

    FYI the font file is at...

    ```text
    nuttx/apps/graphics/lvgl/lvgl/src/font/lv_font_unscii_16.c
    ```

    Which gets compiled to...

    ```text
    nuttx/apps/graphics/lvgl/lv_font_unscii_16.c.*.apps.graphics.lvgl.o
    ```

    Which should define `lv_font_unscii_16`...

    ```bash
    $ aarch64-none-elf-objdump -t -S nuttx/apps/graphics/lvgl/lv_font_unscii_16.c.*.apps.graphics.lvgl.o
    ...
    0000000000000000 g     O .rodata.lv_font_unscii_16	0000000000000030 lv_font_unscii_16
    ```
