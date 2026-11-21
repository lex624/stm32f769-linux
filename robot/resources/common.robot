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


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Initialise System Test
   # Convert the debug True/False passed on the command line to a boolean
   ${DEBUG_ENABLED}=   Convert To Boolean   ${DEBUG}
   Set Global Variable   ${DEBUG_ENABLED}
   # &{OPTIONS} (RF 5.0+) exposes the command line's include and exclude tag patterns
   Log   Bench ${BENCH_NAME}: include ${OPTIONS.include} exclude ${OPTIONS.exclude}   console=True
   # A run that selects hardware tests on a bench without hardware would only
   # produce a wall of connection failures, so stop straight away instead.
   ${excludes_hardware}=   Evaluate   any(pattern.startswith('needs') for pattern in $OPTIONS.exclude)
   IF   not ${HAS_HARDWARE} and not ${excludes_hardware}
      Fatal Error   Bench ${BENCH_NAME} has no hardware: exclude the needs* tags (--exclude needs*). Bailing...
   END

Tear Down System Test
   Log   System test finished on bench ${BENCH_NAME}   console=True

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
