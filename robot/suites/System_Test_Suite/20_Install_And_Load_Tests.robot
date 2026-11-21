*** Settings ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Documentation   The device answers on its console, runs the expected kernel, stops in
...             its bootloader and boots back, and restarts reliably. Device-specific
...             steps live in the adapter resources/devices/${DEVICE_FAMILY}.robot.
Resource   ../../resources/common.robot
Resource   ../../resources/devices/${DEVICE_FAMILY}.robot
Test Tags   critical   postmerge   nightly   weekend   needsboard   needsserial
Test Timeout   20 minutes
Suite Setup   Suite Set Up For Install And Load Testing
Suite Teardown   Suite Tear Down For Install And Load Testing


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
# The bench file overrides these; the defaults let hosted runs parse this suite
${DEVICE_FAMILY}   stm32f769
${EXPECTED_KERNEL_RELEASE}   ${EMPTY}
# Restart count for INS-03 by cadence tag, as in our other system-test suites
${POSTMERGE_RELIABILITY_ITERATIONS}   1
${NIGHTLY_RELIABILITY_ITERATIONS}   3
${WEEKEND_RELIABILITY_ITERATIONS}   10
# A device that has just booted has an uptime well under this
${FRESH_BOOT_MAX_UPTIME}   120


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Suite Set Up For Install And Load Testing
   Require Bench Hardware
   # Configure the number of restarts for the reliability test from the cadence
   # tag we are running with (&{OPTIONS} exposes --include)
   ${iterations}=   Set Variable   ${POSTMERGE_RELIABILITY_ITERATIONS}
   IF   any('nightly' in pattern.lower() for pattern in $OPTIONS.include)
      ${iterations}=   Set Variable   ${NIGHTLY_RELIABILITY_ITERATIONS}
   END
   IF   any('weekend' in pattern.lower() for pattern in $OPTIONS.include)
      ${iterations}=   Set Variable   ${WEEKEND_RELIABILITY_ITERATIONS}
   END
   Set Suite Variable   ${RELIABILITY_ITERATIONS}   ${iterations}
   Open Device Console
   Wait For Device Ready

Suite Tear Down For Install And Load Testing
   # Leave the board at its Linux shell for whoever uses it next, but only if
   # setup got as far as opening the console
   IF   ${CONSOLE_IS_OPEN}
      Run Keyword And Ignore Error   Return Device To Linux
      Close Device Console
   END

Device Should Be Freshly Booted
   [Documentation]   The device is at its shell, booted moments ago, on the expected kernel.
   ${uptime}=   Get Device Uptime
   Should Be True   ${uptime} < ${FRESH_BOOT_MAX_UPTIME}   msg=Uptime ${uptime} s: the device did not reboot
   Device Should Run The Expected Kernel

Device Should Run The Expected Kernel
   ${release}=   Get Device Kernel Release
   Log   Kernel release ${release}   console=True
   IF   '${EXPECTED_KERNEL_RELEASE}' != ''
      Should Be Equal   ${release}   ${EXPECTED_KERNEL_RELEASE}   msg=Wrong kernel on the device
   END


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Test Cases ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
INS-01 DEVICE ANSWERS ON THE CONSOLE WITH THE EXPECTED KERNEL
   [Documentation]   The console shell responds and the running kernel release matches
   ...               the bench's EXPECTED_KERNEL_RELEASE.
   Device Should Run The Expected Kernel

INS-02 DEVICE STOPS IN THE BOOTLOADER AND BOOTS BACK TO LINUX
   [Documentation]   A reset can be caught at the bootloader prompt, the bootloader
   ...               answers a command, and continuing the boot reaches a fresh shell.
   [Tags]   cap-bootloader-console
   Stop At Bootloader
   ${version}=   Run In Bootloader   version
   Should Contain   ${version}   U-Boot
   Log   ${version}   console=True
   Boot From Bootloader
   Device Should Be Freshly Booted

INS-03 DEVICE RESTARTS RELIABLY
   [Documentation]   The device restarts back to a fresh shell on the expected kernel
   ...               every time: once postmerge, 3 times nightly, 10 times at weekends.
   FOR   ${iteration}   IN RANGE   1   ${RELIABILITY_ITERATIONS} + 1
      Log   Restart ${iteration} of ${RELIABILITY_ITERATIONS}   console=True
      Restart Device
      Device Should Be Freshly Booted
   END
