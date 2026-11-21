*** Settings ***
Documentation   General keywords and global constants for the system test. Imported by
...             __init__.robot and every suite: variables and keywords defined in an
...             init file are not visible to child suites, so shared ones live here.
# ALL LIBRARIES USED BY THE ROBOT TEST CASES
Library   Process
Library   OperatingSystem
Library   String
Library   Collections


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
# Override with --variable DEBUG:True
${DEBUG}   False
# The bench variable file (--variablefile robot/benches/<bench>.yaml) overrides these
${BENCH_NAME}   hosted
${HAS_HARDWARE}   ${False}
${REPO_ROOT}   ${{ pathlib.Path(r'${CURDIR}').parents[1].as_posix() }}
${GATEWAY_DIR}   ${REPO_ROOT}/gateway
# Generous: npm ci on a cold cache downloads the whole gateway toolchain
${BUILD_STEP_TIMEOUT}   10 minutes
# CI sets this to True so a missing tool fails the run; locally a missing tool
# (no gcc on Windows, say) skips the test instead
${REQUIRE_ALL_TOOLS}   False
${HOST_BUILD_DIR}   ${OUTPUT DIR}/host-build
# Host compile flags for the board's C tools: the board build adds -mcpu and elf2flt
${HOST_CFLAGS}   -std=gnu11 -Wall -Wextra -Werror -O2


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Initialise System Test
   # Convert the debug True/False passed on the command line to a boolean
   ${DEBUG_ENABLED}=   Convert To Boolean   ${DEBUG}
   Set Global Variable   ${DEBUG_ENABLED}
   # &{OPTIONS} (RF 5.0+) exposes the command line's include and exclude tag patterns
   Log   Bench ${BENCH_NAME}: include ${OPTIONS.include} exclude ${OPTIONS.exclude}   console=True

Require Bench Hardware
   [Documentation]   Suite setup step for every suite with needs* tests. Running them on
   ...               a bench without hardware would only produce a wall of connection
   ...               failures, so stop the whole run straight away instead. Kept out of
   ...               __init__.robot so build-only runs (CI, the editor) never trip it.
   IF   not ${HAS_HARDWARE}
      Fatal Error   Bench ${BENCH_NAME} has no hardware for this suite: use a hardware bench file or --exclude needs*. Bailing...
   END

Tear Down System Test
   Log   System test finished on bench ${BENCH_NAME}   console=True

Require Tool
   [Documentation]   Checks ${tool} runs (``${tool} ${version_args}``). When it does not,
   ...               fails if ${REQUIRE_ALL_TOOLS} is true (CI) and skips otherwise (local).
   [Arguments]   ${tool}   ${version_args}=--version
   ${result}=   Run Process   ${tool} ${version_args}   shell=True   stderr=STDOUT
   IF   ${result.rc} != 0
      IF   ${REQUIRE_ALL_TOOLS}
         Fail   ${tool} is required on this runner but is not installed
      ELSE
         Skip   ${tool} is not installed here; CI runs this test
      END
   END
   ${first_line}=   Get Line   ${result.stdout}   0
   Log   ${tool}: ${first_line}

Build Host Tool
   [Documentation]   Compiles one of the board's C tools for the host with ${HOST_CFLAGS}
   ...               and returns the executable's path. Host builds catch warnings and
   ...               let the host run the same code the board runs.
   [Arguments]   ${source}   ${name}
   Require Tool   gcc
   Create Directory   ${HOST_BUILD_DIR}
   Run Build Step   gcc ${HOST_CFLAGS} -o "${HOST_BUILD_DIR}/${name}" "${REPO_ROOT}/${source}"   ${REPO_ROOT}
   RETURN   ${HOST_BUILD_DIR}/${name}

Run Build Step
   [Documentation]   Runs one build or test command in ${cwd} and fails with its output if it fails.
   ...               Uses the shell so npm resolves to npm.cmd on Windows and npm on Linux.
   ...               Returns the process result so callers can check its output.
   [Arguments]   ${command}   ${cwd}   ${timeout}=${BUILD_STEP_TIMEOUT}
   Log   ${command} (in ${cwd})   console=True
   ${result}=   Run Process   ${command}   shell=True   cwd=${cwd}   timeout=${timeout}   stderr=STDOUT
   Log   ${result.stdout}
   Should Be Equal As Integers   ${result.rc}   0   msg=${command} failed (rc=${result.rc}):${\n}${result.stdout}   values=False
   RETURN   ${result}
