*** Settings ***
Documentation   General keywords and global constants for the F769 system test
# ALL LIBRARIES USED BY THE ROBOT TEST CASES
Library   Process
Library   OperatingSystem
Library   String
Library   Collections


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
# Supplied by run_robot.py; the defaults let a bare `robot` invocation work too
${REPO_ROOT}   ${CURDIR}/../..
${INCLUDE_TAGS}   ${EMPTY}
${EXCLUDE_TAGS}   ${EMPTY}
${DEBUG}   False
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
   Log   Bench ${BENCH_NAME}: include '${INCLUDE_TAGS}' exclude '${EXCLUDE_TAGS}'   console=True
   # A run that selects hardware tests on a bench without hardware would only
   # produce a wall of connection failures, so stop straight away instead.
   ${wants_hardware}=   Evaluate   'needs' not in '''${EXCLUDE_TAGS}''' and not ${HAS_HARDWARE}
   Run Keyword If   ${wants_hardware}   Fatal Error   Bench ${BENCH_NAME} has no hardware: exclude the needs* tags (-e "needs*"). Bailing...

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
