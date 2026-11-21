*** Settings ***
Documentation   Device adapter: STM32F769I-DISCO running the noMMU Linux image with
...             U-Boot on the ST-LINK serial console. Implements the device keyword
...             interface from robot/PLAN.md, so suites never branch on the family.
...
...             Board facts this adapter encodes:
...             - every exec needs one contiguous block, so board reads use hush
...               built-ins (read, echo), which work even when nothing can exec;
...             - writes must be synced (sysrq s) before any reset, or the card loses
...               the tail of recently written files.
Resource   ../serial_console.robot


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
${SHELL_PROMPT}   / #${SPACE}
${BOOTLOADER_PROMPT}   U-Boot >${SPACE}
${AUTOBOOT_TEXT}   Hit SPACE
# First text U-Boot prints after a reset
${RESET_BANNER}   U-Boot 20
${SHELL_BANNER}   the humble shell
# Reset to shell takes about 15 s on this board; allow for a slow card
${BOOT_TIMEOUT}   120


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Wait For Device Ready
   [Documentation]   Returns once the Linux shell answers on the console. Works whether
   ...               the board is already up (one Enter) or still booting.
   # Ctrl-C clears any half-typed line, then Enter asks for a fresh prompt
   Write Data   \x03
   Send Console Line   ${EMPTY}
   ${ready}=   Console Text Appears   ${SHELL_PROMPT}   5
   IF   not ${ready}
      Log   No prompt yet; waiting up to ${BOOT_TIMEOUT} s for the shell   console=True
      Wait For Console Text   ${SHELL_BANNER}   ${BOOT_TIMEOUT}
      Send Console Line   ${EMPTY}
      Wait For Console Text   ${SHELL_PROMPT}   ${CONSOLE_TIMEOUT}
   END
   # Right after boot the kernel can still be printing (service start-up, and on
   # this noMMU board page-allocation-failure dumps when an exec finds no
   # contiguous block). Let that settle before typing commands into it.
   Wait For Console Quiet

Run On Device
   [Documentation]   Runs a shell command on the console and returns its output.
   ...               Prefer built-ins for anything that must work under memory pressure.
   [Arguments]   ${command}   ${timeout}=${CONSOLE_TIMEOUT}
   ${output}=   Run Console Command   ${command}   ${SHELL_PROMPT}   ${timeout}
   RETURN   ${output}

Read Device File
   [Documentation]   Returns the first line of ${path}, read with the shell's built-in
   ...               read so it works even when no program can be started.
   [Arguments]   ${path}
   ${output}=   Run On Device   read v < ${path}; echo "<<$v>>"
   ${value}=   Get Regexp Matches   ${output}   <<(.*)>>   1
   Should Not Be Empty   ${value}   msg=Could not read ${path}: ${output}
   RETURN   ${value}[0]

Get Device Kernel Release
   ${release}=   Read Device File   /proc/sys/kernel/osrelease
   RETURN   ${release}

Get Device Uptime
   [Documentation]   Seconds since the kernel started, as a number.
   ${uptime}=   Read Device File   /proc/uptime
   ${seconds}=   Evaluate   float($uptime.split()[0])
   RETURN   ${seconds}

Sync Device Storage
   [Documentation]   Emergency sync, waiting for the kernel to confirm it has finished:
   ...               sysrq s only queues the writeback. A console character can still be
   ...               lost while the kernel is printing, so the command is retried until
   ...               the kernel confirms it.
   FOR   ${attempt}   IN RANGE   1   4
      Drain Console
      Send Console Line   echo s > /proc/sysrq-trigger
      ${synced}=   Console Text Appears   Emergency Sync complete   30
      IF   ${synced}   RETURN
      Log   Sync command did not take (attempt ${attempt}); retrying   level=WARN
   END
   Fail   The device never confirmed the emergency sync

Reset Device
   [Documentation]   Syncs, then resets the board (sysrq b) and returns once the
   ...               bootloader's banner shows the reset happened. Retried like the sync.
   Sync Device Storage
   FOR   ${attempt}   IN RANGE   1   4
      Drain Console
      Send Console Line   echo b > /proc/sysrq-trigger
      ${reset}=   Console Text Appears   ${RESET_BANNER}   15
      IF   ${reset}   RETURN
      Log   Reset command did not take (attempt ${attempt}); retrying   level=WARN
   END
   Fail   The device did not reset

Restart Device
   Reset Device
   Wait For Device Ready

Stop At Bootloader
   [Documentation]   Resets the board and stops U-Boot's autoboot at its prompt.
   Reset Device
   Wait For Console Text   ${AUTOBOOT_TEXT}   60
   Write Data   ${SPACE}
   Wait For Console Text   ${BOOTLOADER_PROMPT}   10

Run In Bootloader
   [Documentation]   Runs a U-Boot command and returns its output.
   [Arguments]   ${command}   ${timeout}=${CONSOLE_TIMEOUT}
   ${output}=   Run Console Command   ${command}   ${BOOTLOADER_PROMPT}   ${timeout}
   RETURN   ${output}

Boot From Bootloader
   [Documentation]   Continues the normal boot from the U-Boot prompt to the Linux shell.
   Send Console Line   boot
   Wait For Device Ready

Return Device To Linux
   [Documentation]   Teardown helper: leaves the board at its Linux shell whatever state
   ...               a failed test left it in (U-Boot prompt, mid-boot or running).
   # An empty line makes either prompt reappear without starting a program (on
   # the Linux side every exec costs a contiguous block this board may not have)
   Drain Console
   Send Console Line   ${EMPTY}
   ${at_bootloader}=   Console Text Appears   ${BOOTLOADER_PROMPT}   3
   IF   ${at_bootloader}
      Log   Board was left in U-Boot; booting Linux   level=WARN
      Boot From Bootloader
   ELSE
      Wait For Device Ready
   END
