/****************************************************************************
 * apps/examples/lvglterm/lvglterm.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/boardctl.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <debug.h>
#include <poll.h>
#include <lvgl/lvgl.h>
#include <port/lv_port.h>
#include "nshlib/nshlib.h"

static void create_terminal(void);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Should we perform board-specific driver initialization?  There are two
 * ways that board initialization can occur:  1) automatically via
 * board_late_initialize() during bootupif CONFIG_BOARD_LATE_INITIALIZE
 * or 2).
 * via a call to boardctl() if the interface is enabled
 * (CONFIG_BOARDCTL=y).
 * If this task is running as an NSH built-in application, then that
 * initialization has probably already been performed otherwise we do it
 * here.
 */

#undef NEED_BOARDINIT

#if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#  define NEED_BOARDINIT 1
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main or lvglterm_main
 *
 * Description:
 *
 * Input Parameters:
 *   Standard argc and argv
 *
 * Returned Value:
 *   Zero on success; a positive, non-zero value on failure.
 *
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
#ifdef NEED_BOARDINIT
  /* Perform board-specific driver initialization */

  boardctl(BOARDIOC_INIT, 0);

#ifdef CONFIG_BOARDCTL_FINALINIT
  /* Perform architecture-specific final-initialization (if configured) */

  boardctl(BOARDIOC_FINALINIT, 0);
#endif
#endif

  /* LVGL initialization */

  lv_init();

  /* LVGL port initialization */

  lv_port_init();

  /* Create an LVGL Terminal that will let us interact with NuttX NSH Shell */

  create_terminal();

  /* Handle LVGL tasks */

  while (1)
    {
      uint32_t idle;
      idle = lv_timer_handler();

      /* Minimum sleep of 1ms */

      idle = idle ? idle : 1;
      usleep(idle * 1000);
    }

  return EXIT_SUCCESS;
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifndef CONFIG_DEV_PIPE_SIZE
#error Please enable "Device Drivers > FIFO and named pipe drivers" in menuconfig
#endif

static bool has_input(int fd);
static void timer_callback(lv_timer_t * timer);
static void create_widgets(void);
static void input_callback(lv_event_t * e);
static void remove_escape_codes(char *buf, int len);

// Pipes for NSH Shell: : stdin, stdout, stderr
static int nsh_stdin[2];
static int nsh_stdout[2];
static int nsh_stderr[2];

#define READ_PIPE  0  // Read Pipes: stdin, stdout, stderr
#define WRITE_PIPE 1  // Write Pipes: stdin, stdout, stderr

// LVGL Text Areas for NSH Input and Output
static lv_obj_t *input;
static lv_obj_t *output;

// Create an LVGL Terminal that will let us interact with NuttX NSH Shell
static void create_terminal(void) {
  _info("create_terminal\n");

  // Create the pipes for NSH Shell
  int ret;
  ret = pipe(nsh_stdin);  if (ret < 0) { _err("stdin pipe failed: %d\n", errno);  return; }
  ret = pipe(nsh_stdout); if (ret < 0) { _err("stdout pipe failed: %d\n", errno); return; }
  ret = pipe(nsh_stderr); if (ret < 0) { _err("stderr pipe failed: %d\n", errno); return; }

  // Close default stdin, stdout and stderr
  close(0);
  close(1);
  close(2);

  // Use the pipes as NSH stdin, stdout and stderr
  dup2(nsh_stdin[READ_PIPE], 0);
  dup2(nsh_stdout[WRITE_PIPE], 1);
  dup2(nsh_stderr[WRITE_PIPE], 2);

  // Create a new NSH Console using the pipes
  char *argv[] = { NULL };
  pid_t pid = task_create(
    "NSH Console",
    100,  // Priority
    CONFIG_DEFAULT_TASK_STACKSIZE,
    nsh_consolemain,
    argv
  );
  if (pid < 0) { _err("task_create failed: %d\n", errno); return; }
  _info("pid=%d\n", pid);

  // Create an LVGL Timer to poll for output from NSH Shell
  // Based on https://docs.lvgl.io/master/overview/timer.html#create-a-timer
  static uint32_t user_data = 10;
  lv_timer_t *timer = lv_timer_create(
    timer_callback,  // Callback Function
    100,        // Timer Period (Milliseconds)
    &user_data  // Callback Data
  );
  UNUSED(timer);

  // Create the LVGL Terminal Widgets
  create_widgets();
}

// Callback Function for LVGL Timer.
// Based on https://docs.lvgl.io/master/overview/timer.html#create-a-timer
static void timer_callback(lv_timer_t *timer) {
  int ret;

  // Poll NSH stdout to check if there's output to be processed
  static char buf[64];
  DEBUGASSERT(nsh_stdout[READ_PIPE] != 0);
  if (has_input(nsh_stdout[READ_PIPE])) {

    // Read the output from NSH stdout
    ret = read(
      nsh_stdout[READ_PIPE],
      buf,
      sizeof(buf) - 1
    );

    // Add to NSH Output Text Area
    if (ret > 0) {
      buf[ret] = 0;
      _info("%s\n", buf); 
      infodumpbuffer("timer_callback", (const uint8_t *)buf, ret);

      remove_escape_codes(buf, ret);
      DEBUGASSERT(output != NULL);
      lv_textarea_add_text(output, buf);
    }
  }

  // Poll NSH stderr to check if there's output to be processed
  DEBUGASSERT(nsh_stderr[READ_PIPE] != 0);
  if (has_input(nsh_stderr[READ_PIPE])) {

    // Read the output from NSH stderr
    ret = read(    
      nsh_stderr[READ_PIPE],
      buf,
      sizeof(buf) - 1
    );

    // Add to NSH Output Text Area
    if (ret > 0) {
      buf[ret] = 0;
      _info("%s\n", buf); 
      infodumpbuffer("timer_callback", (const uint8_t *)buf, ret);

      remove_escape_codes(buf, ret);
      DEBUGASSERT(output != NULL);
      lv_textarea_add_text(output, buf);
    }
  }
}

#ifndef CONFIG_LV_FONT_UNSCII_16
#error Please enable "LVGL configuration > Font usage > Enable built-in fonts > UNSCII 16" in menuconfig
#endif

// PinePhone LCD Panel Width and Height (pixels)
#define PINEPHONE_LCD_PANEL_WIDTH  720
#define PINEPHONE_LCD_PANEL_HEIGHT 1440

// Margin of 10 pixels all around
#define TERMINAL_MARGIN 10

// Terminal Width is LCD Width minus Left and Right Margins
#define TERMINAL_WIDTH  (PINEPHONE_LCD_PANEL_WIDTH - 2 * TERMINAL_MARGIN)

// Keyboard is Lower Half of LCD.
// Terminal Height is Upper Half of LCD minus Top and Bottom Margins.
#define TERMINAL_HEIGHT ((PINEPHONE_LCD_PANEL_HEIGHT / 2) - 2 * TERMINAL_MARGIN)

// Height of Input Text Area
#define INPUT_HEIGHT 100

// Height of Output Text Area is Terminal Height minus Input Height minus Middle Margin
#define OUTPUT_HEIGHT (TERMINAL_HEIGHT - INPUT_HEIGHT - TERMINAL_MARGIN)

// Create the LVGL Widgets for the LVGL Terminal.
// Based on https://docs.lvgl.io/master/widgets/keyboard.html#keyboard-with-text-area
static void create_widgets(void) {

  // Create an LVGL Keyboard Widget
  lv_obj_t *kb = lv_keyboard_create(lv_scr_act());

  // Set the Font Style for NSH Input and Output to a Monospaced Font
  static lv_style_t terminal_style;
  lv_style_init(&terminal_style);
  lv_style_set_text_font(&terminal_style, &lv_font_unscii_16);

  // Create an LVGL Text Area Widget for NSH Output
  output = lv_textarea_create(lv_scr_act());
  lv_obj_add_style(output, &terminal_style, 0);
  lv_obj_align(output, LV_ALIGN_TOP_LEFT, TERMINAL_MARGIN, TERMINAL_MARGIN);
  lv_textarea_set_placeholder_text(output, "Hello");
  lv_obj_set_size(output, TERMINAL_WIDTH, OUTPUT_HEIGHT);

  // Create an LVGL Text Area Widget for NSH Input
  input = lv_textarea_create(lv_scr_act());
  lv_obj_add_style(input, &terminal_style, 0);
  lv_obj_align(input, LV_ALIGN_TOP_LEFT, TERMINAL_MARGIN, OUTPUT_HEIGHT + 2 * TERMINAL_MARGIN);
  lv_obj_add_event_cb(input, input_callback, LV_EVENT_ALL, kb);
  lv_obj_set_size(input, TERMINAL_WIDTH, INPUT_HEIGHT);

  // Set the Keyboard to populate the NSH Input Text Area
  lv_keyboard_set_textarea(kb, input);
}

// Callback Function for NSH Input Text Area.
// Based on https://docs.lvgl.io/master/widgets/keyboard.html#keyboard-with-text-area
static void input_callback(lv_event_t *e) {
  int ret;

  // Decode the LVGL Event
  const lv_event_code_t code = lv_event_get_code(e);
  // const lv_obj_t *ta = lv_event_get_target(e);
  // _info("code=%d\n", code);

  // If Enter has been pressed, send the Command to NSH Input
  if (code == LV_EVENT_VALUE_CHANGED) {

    // Get the Keyboard Widget from the LVGL Event
    const lv_obj_t *kb = lv_event_get_user_data(e);
    DEBUGASSERT(kb != NULL);

    // Get the Button Index of the Keyboard Button Pressed
    const uint16_t id = lv_keyboard_get_selected_btn(kb);
    // _info("btn=%d\n", id);

    // Get the Text of the Keyboard Button
    const char *key = lv_keyboard_get_btn_text(kb, id);
    if (key == NULL) { return; }
    _info("key[0]=%d, key=%s\n", key[0], key);
    infodumpbuffer("input_callback", (const uint8_t *)key, strlen(key));

    // If Enter is pressed...
    if (key[0] == 0xef && key[1] == 0xa2 && key[2] == 0xa2) {

      // Read the NSH Input
      DEBUGASSERT(input != NULL);
      const char *cmd = lv_textarea_get_text(input);
      if (cmd == NULL || cmd[0] == 0) { return; }
      infodumpbuffer("input_callback", (const uint8_t *)cmd, strlen(cmd));

      // Send the Command to NSH stdin
      DEBUGASSERT(nsh_stdin[WRITE_PIPE] != 0);
      ret = write(
        nsh_stdin[WRITE_PIPE],
        cmd,
        strlen(cmd)
      );
      _info("write nsh_stdin: %d\n", ret);

      // Erase the NSH Input
      lv_textarea_set_text(input, "");
    }
  }
}

// Return true if the File Descriptor has data to be read
static bool has_input(int fd) {

  // Poll the File Descriptor for Input
  struct pollfd fdp;
  fdp.fd = fd;
  fdp.events = POLLIN;
  int ret = poll(
    (struct pollfd *)&fdp,  // File Descriptors
    1,  // Number of File Descriptors
    0   // Poll Timeout (Milliseconds)
  );

  if (ret > 0) {
    // If Poll is OK and there is Input...
    if ((fdp.revents & POLLIN) != 0) {
      // Report that there's Input
      // _info("has input: fd=%d\n", fd);
      return true;
    }

    // Else report No Input
    // _info("no input: fd=%d\n", fd);
    return false;

  } else if (ret == 0) {
    // Timeout means nothing to read
    // _info("timeout: fd=%d\n", fd);
    return false;

  } else if (ret < 0) {
    // Handle Error
    _err("poll failed: %d, fd=%d\n", ret, fd);
    return false;
  }

  // Never comes here
  DEBUGASSERT(false);
  return false;
}

// Remove Escape Codes from the string and replace by spaces
static void remove_escape_codes(char *buf, int len) {
  for (int i = 0; i < len; i++) {
    // Escape Code looks like 0x1b 0x5b 0x4b
    if (buf[i] == 0x1b) {
      // Replace the 3 bytes with spaces
      buf[i] = ' ';
      if (i + 1 < len) { buf[i + 1] = ' '; }
      if (i + 2 < len) { buf[i + 2] = ' '; }
    }
  }
}

/* Output:
DRAM: 2048 MiB
Trying to boot from MMC1
NOTICE:  BL31: v2.2(release):v2.2-904-gf9ea3a629
NOTICE:  BL31: Built : 15:32:12, Apr  9 2020
NOTICE:  BL31: Detected Allwinner A64/H64/R18 SoC (1689)
NOTICE:  BL31: Found U-Boot DTB at 0x4064410, model: PinePhone
NOTICE:  PSCI: System suspend is unavailable


U-Boot 2020.07 (Nov 08 2020 - 00:15:12 +0100)

DRAM:  2 GiB
MMC:   Device 'mmc@1c11000': seq 1 is in use by 'mmc@1c10000'
mmc@1c0f000: 0, mmc@1c10000: 2, mmc@1c11000: 1
Loading Environment from FAT... *** Warning - bad CRC, using default environment

starting USB...
No working controllers found
Hit any key to stop autoboot:  0 
switch to partitions #0, OK
mmc0 is current device
Scanning mmc 0:1...
Found U-Boot script /boot.scr
653 bytes read in 3 ms (211.9 KiB/s)
## Executing script at 4fc00000
gpio: pin 114 (gpio 114) value is 1
291400 bytes read in 17 ms (16.3 MiB/s)
Uncompressed size: 10416128 = 0x9EF000
36162 bytes read in 4 ms (8.6 MiB/s)
1078500 bytes read in 50 ms (20.6 MiB/s)
## Flattened Device Tree blob at 4fa00000
   Booting using the fdt blob at 0x4fa00000
   Loading Ramdisk to 49ef8000, end 49fff4e4 ... OK
   Loading Device Tree to 0000000049eec000, end 0000000049ef7d41 ... OK

Starting kernel ...

- Ready to Boot CPU
- Boot from EL2
- Boot from EL1
- Boot to C runtime for OS Initialize
create_terminal: create_terminal
create_terminal: pid=3
timer_callback: 
NuttShell (NSH) NuttX-12.0.0
nsh> 
timer_callback (0x40110eb8):
0000  0a 4e 75 74 74 53 68 65 6c 6c 20 28 4e 53 48 29  .NuttShell (NSH)
0010  20 4e 75 74 74 58 2d 31 32 2e 30 2e 30 0a 6e 73   NuttX-12.0.0.ns
0020  68 3e 20 1b 5b 4b                                h> .[K          
input_callback: key[0]=117, key=u
input_callback (0x400fd847):
0000  75                                               u               
input_callback: key[0]=110, key=n
input_callback (0x400fd9b7):
0000  6e                                               n               
input_callback: key[0]=97, key=a
input_callback (0x400fd9e4):
0000  61                                               a               
input_callback: key[0]=109, key=m
input_callback (0x400fc4eb):
0000  6d                                               m               
input_callback: key[0]=101, key=e
input_callback (0x400fd9c0):
0000  65                                               e               
input_callback: key[0]=32, key= 
input_callback (0x400fd902):
0000  20                                                               
input_callback: key[0]=45, key=-
input_callback (0x400fd5bf):
0000  2d                                               -               
input_callback: key[0]=97, key=a
input_callback (0x400fd9e4):
0000  61                                               a               
input_callback: key[0]=239, key=Ô¢¢
input_callback (0x400fc84a):
0000  ef a2 a2                                         ...             
input_callback (0x40113a00):
0000  75 6e 61 6d 65 20 2d 61 0a                       uname -a.       
input_callback: write nsh_stdin: 9
input_callback: key[0]=239, key=Ô¢¢
input_callback (0x400fc84a):
0000  ef a2 a2                                         ...             
timer_callback: uname -a
NuttX 12.0.0 bd6a0b0 Jan 25 2023 07:49:44 arm64 pineph
timer_callback (0x40110eb8):
0000  75 6e 61 6d 65 20 2d 61 0a 4e 75 74 74 58 20 31  uname -a.NuttX 1
0010  32 2e 30 2e 30 20 62 64 36 61 30 62 30 20 4a 61  2.0.0 bd6a0b0 Ja
0020  6e 20 32 35 20 32 30 32 33 20 30 37 3a 34 39 3a  n 25 2023 07:49:
0030  34 34 20 61 72 6d 36 34 20 70 69 6e 65 70 68     44 arm64 pineph 
timer_callback: one
nsh> 
timer_callback (0x40110eb8):
0000  6f 6e 65 0a 6e 73 68 3e 20 1b 5b 4b              one.nsh> .[K    
input_callback: key[0]=108, key=l
input_callback (0x400fddc2):
0000  6c                                               l               
input_callback: key[0]=115, key=s
input_callback (0x400fcd0f):
0000  73                                               s               
input_callback: key[0]=32, key= 
input_callback (0x400fd902):
0000  20                                                               
input_callback: key[0]=45, key=-
input_callback (0x400fd5bf):
0000  2d                                               -               
input_callback: key[0]=108, key=l
input_callback (0x400fddc2):
0000  6c                                               l               
input_callback: key[0]=239, key=Ô¢¢
input_callback (0x400fc84a):
0000  ef a2 a2                                         ...             
input_callback (0x401135b8):
0000  6c 73 20 2d 6c 0a                                ls -l.          
input_callback: write nsh_stdin: 6
input_callback: key[0]=239, key=Ô¢¢
input_callback (0x400fc84a):
0000  ef a2 a2                                         ...             
timer_callback: ls -l
/:
 dr--r--r--       0 dev/
 dr--r--r--       0 proc/
 dr
timer_callback (0x40110eb8):
0000  6c 73 20 2d 6c 0a 2f 3a 0a 20 64 72 2d 2d 72 2d  ls -l./:. dr--r-
0010  2d 72 2d 2d 20 20 20 20 20 20 20 30 20 64 65 76  -r--       0 dev
0020  2f 0a 20 64 72 2d 2d 72 2d 2d 72 2d 2d 20 20 20  /. dr--r--r--   
0030  20 20 20 20 30 20 70 72 6f 63 2f 0a 20 64 72         0 proc/. dr 
timer_callback: --r--r--       0 var/
nsh> 
timer_callback (0x40110eb8):
0000  2d 2d 72 2d 2d 72 2d 2d 20 20 20 20 20 20 20 30  --r--r--       0
0010  20 76 61 72 2f 0a 6e 73 68 3e 20 1b 5b 4b         var/.nsh> .[K  
input_callback: key[0]=112, key=p
input_callback (0x400fe0de):
0000  70                                               p               
input_callback: key[0]=115, key=s
input_callback (0x400fcd0f):
0000  73                                               s               
input_callback: key[0]=239, key=Ô¢¢
input_callback (0x400fc84a):
0000  ef a2 a2                                         ...             
input_callback (0x401135b8):
0000  70 73 0a                                         ps.             
input_callback: write nsh_stdin: 3
input_callback: key[0]=239, key=Ô¢¢
input_callback (0x400fc84a):
0000  ef a2 a2                                         ...             
timer_callback: ps
  PID GROUP PRI POLICY   TYPE    NPX STATE    EVENT     SIGM
timer_callback (0x40110eb8):
0000  70 73 0a 20 20 50 49 44 20 47 52 4f 55 50 20 50  ps.  PID GROUP P
0010  52 49 20 50 4f 4c 49 43 59 20 20 20 54 59 50 45  RI POLICY   TYPE
0020  20 20 20 20 4e 50 58 20 53 54 41 54 45 20 20 20      NPX STATE   
0030  20 45 56 45 4e 54 20 20 20 20 20 53 49 47 4d      EVENT     SIGM 
timer_callback: ASK   STACK   USED  FILLED COMMAND
    0     0   0 FIFO     Kth
timer_callback (0x40110eb8):
0000  41 53 4b 20 20 20 53 54 41 43 4b 20 20 20 55 53  ASK   STACK   US
0010  45 44 20 20 46 49 4c 4c 45 44 20 43 4f 4d 4d 41  ED  FILLED COMMA
0020  4e 44 0a 20 20 20 20 30 20 20 20 20 20 30 20 20  ND.    0     0  
0030  20 30 20 46 49 46 4f 20 20 20 20 20 4b 74 68      0 FIFO     Kth 
timer_callback: read N-- Ready              00000000 008144 000848  10.4%  Idle
timer_callback (0x40110eb8):
0000  72 65 61 64 20 4e 2d 2d 20 52 65 61 64 79 20 20  read N-- Ready  
0010  20 20 20 20 20 20 20 20 20 20 20 20 30 30 30 30              0000
0020  30 30 30 30 20 30 30 38 31 34 34 20 30 30 30 38  0000 008144 0008
0030  34 38 20 20 31 30 2e 34 25 20 20 49 64 6c 65     48  10.4%  Idle 
timer_callback:  Task
    1     1 192 RR       Kthread --- Waiting  Semaphore 0
timer_callback (0x40110eb8):
0000  20 54 61 73 6b 0a 20 20 20 20 31 20 20 20 20 20   Task.    1     
0010  31 20 31 39 32 20 52 52 20 20 20 20 20 20 20 4b  1 192 RR       K
0020  74 68 72 65 61 64 20 2d 2d 2d 20 57 61 69 74 69  thread --- Waiti
0030  6e 67 20 20 53 65 6d 61 70 68 6f 72 65 20 30     ng  Semaphore 0 
timer_callback: 0000000 008096 000976  12.0%  hpwork 0x40110e20
    2     2 100
timer_callback (0x40110eb8):
0000  30 30 30 30 30 30 30 20 30 30 38 30 39 36 20 30  0000000 008096 0
0010  30 30 39 37 36 20 20 31 32 2e 30 25 20 20 68 70  00976  12.0%  hp
0020  77 6f 72 6b 20 30 78 34 30 31 31 30 65 32 30 0a  work 0x40110e20.
0030  20 20 20 20 32 20 20 20 20 20 32 20 31 30 30         2     2 100 
timer_callback:  RR       Task    --- Waiting  Signal    00000000 008112 003344
timer_callback (0x40110eb8):
0000  20 52 52 20 20 20 20 20 20 20 54 61 73 6b 20 20   RR       Task  
0010  20 20 2d 2d 2d 20 57 61 69 74 69 6e 67 20 20 53    --- Waiting  S
0020  69 67 6e 61 6c 20 20 20 20 30 30 30 30 30 30 30  ignal    0000000
0030  30 20 30 30 38 31 31 32 20 30 30 33 33 34 34     0 008112 003344 
timer_callback:   41.2%  lvgldemo_main
    3     3 100 RR       Task    --- Run
timer_callback (0x40110eb8):
0000  20 20 34 31 2e 32 25 20 20 6c 76 67 6c 64 65 6d    41.2%  lvgldem
0010  6f 5f 6d 61 69 6e 0a 20 20 20 20 33 20 20 20 20  o_main.    3    
0020  20 33 20 31 30 30 20 52 52 20 20 20 20 20 20 20   3 100 RR       
0030  54 61 73 6b 20 20 20 20 2d 2d 2d 20 52 75 6e     Task    --- Run 
timer_callback: ning            00000000 008112 002608  32.1%  NSH Console
nsh>
timer_callback (0x40110eb8):
0000  6e 69 6e 67 20 20 20 20 20 20 20 20 20 20 20 20  ning            
0010  30 30 30 30 30 30 30 30 20 30 30 38 31 31 32 20  00000000 008112 
0020  30 30 32 36 30 38 20 20 33 32 2e 31 25 20 20 4e  002608  32.1%  N
0030  53 48 20 43 6f 6e 73 6f 6c 65 0a 6e 73 68 3e     SH Console.nsh> 
timer_callback:  
timer_callback (0x40110eb8):
0000  20 1b 5b 4b                                       .[K            
*/
