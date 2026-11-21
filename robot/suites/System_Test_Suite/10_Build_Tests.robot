*** Settings ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Documentation   Build and static checks that need no hardware. These run on every
...             push and pull request (premerge) on GitHub-hosted runners.
Resource   ../../resources/common.robot
Test Tags   critical   premerge   postmerge   nightly   weekend
Test Timeout   15 minutes
Suite Setup   Suite Set Up For Build Testing


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
# Every C program that runs on the board, as source path and host binary name
&{BOARD_C_TOOLS}
...   frame-sink=bench/frame-sink.c
...   usb-tether=bench/usb-tether.c
...   ssh-listener=bench/ssh-listener.c
...   wifi-scan=bench/wifi-scan.c
...   bluenrg-probe=bench/bluenrg-probe.c
...   f769-control=port/f769-control.c
...   lcd-shell=external/package/lcd-shell/src/lcd-shell.c
...   f769-hello=external/package/f769-hello/src/f769-hello.c
# Shell scripts that run on the board or in the Buildroot build
@{SHELL_SCRIPTS}
...   external/rootfs-overlay/etc/init.d/S30wifi
...   external/rootfs-overlay/etc/init.d/S41bench-link
...   external/rootfs-overlay/etc/init.d/S50usb-tether
...   external/rootfs-overlay/usr/share/udhcpc/default.script
...   external/post-build.sh
...   bench/build-lsh.sh


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Suite Set Up For Build Testing
   Directory Should Exist   ${GATEWAY_DIR}
   Set Suite Variable   ${GATEWAY_DEPS_INSTALLED}   ${False}

Use Gateway Toolchain
   [Documentation]   Checks Node.js is present (skip locally, fail in CI) and installs
   ...               exactly what package-lock.json pins, once per run. Done per test,
   ...               not in suite setup, so a missing Node.js does not skip the C,
   ...               shell, Python and secret checks.
   Require Tool   node
   IF   not ${GATEWAY_DEPS_INSTALLED}
      Run Build Step   npm ci --no-audit --no-fund   ${GATEWAY_DIR}
      Set Suite Variable   ${GATEWAY_DEPS_INSTALLED}   ${True}
   END


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Test Cases ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
BLD-01 GATEWAY UNIT TESTS PASS
   [Documentation]   The gateway's node:test suite (control decoding, frame encoding,
   ...               board URLs and the frame send loop) passes with no failures.
   Use Gateway Toolchain
   ${result}=   Run Build Step   npm test   ${GATEWAY_DIR}
   # node --test prints a summary ("ℹ pass 6" or "# pass 6" depending on the
   # reporter); make sure tests actually ran, not just that npm returned 0
   Should Match Regexp   ${result.stdout}   (?m)\\bpass [1-9]\\d*\\s*$
   Should Match Regexp   ${result.stdout}   (?m)\\bfail 0\\s*$

BLD-02 GATEWAY BUNDLE BUILDS
   [Documentation]   The gateway page bundles with Vite into gateway/dist, which is
   ...               what the phone loads when it is served over HTTPS.
   Use Gateway Toolchain
   Run Build Step   npm run build   ${GATEWAY_DIR}
   File Should Exist   ${GATEWAY_DIR}/dist/index.html

BLD-03 BOARD C TOOLS BUILD WITHOUT WARNINGS
   [Documentation]   Every C program that runs on the board compiles for the host with
   ...               -Wall -Wextra -Werror. The board build uses the same sources with
   ...               the Buildroot toolchain, so warnings caught here are real.
   ...               All tools are built and reported, not just the first failure.
   Require Tool   gcc
   FOR   ${name}   ${source}   IN   &{BOARD_C_TOOLS}
      Run Keyword And Continue On Failure   Build Host Tool   ${source}   ${name}
   END

BLD-04 SHELL SCRIPTS PASS SHELLCHECK
   [Documentation]   Init scripts, the udhcpc hook and the build scripts have no
   ...               shellcheck findings at warning level or above.
   Require Tool   shellcheck
   ${scripts}=   Catenate   @{SHELL_SCRIPTS}
   Run Build Step   shellcheck --severity=warning ${scripts}   ${REPO_ROOT}

BLD-05 PYTHON PASSES RUFF
   [Documentation]   The bench and Robot Python code has no ruff findings.
   Require Tool   ruff
   Run Build Step   ruff check bench robot/lib   ${REPO_ROOT}

BLD-06 NO SECRETS IN THE REPOSITORY
   [Documentation]   gitleaks finds no credentials in the files or the git history:
   ...               Wi-Fi passwords, API secrets and keys must never be committed
   ...               (the board's wpa_supplicant.conf stays on its card).
   Require Tool   gitleaks   version
   Run Build Step   gitleaks detect --no-banner --redact --source .   ${REPO_ROOT}
