*** Settings ***
Documentation   STM32F769I-DISCO Linux bench: build, install, platform, connectivity,
...             USB host, control interface, telemetry and LiveKit end-to-end tests.
...             See robot/PLAN.md for the sections and tags.
Resource   ../../resources/common.robot
Suite Setup   Initialise System Test
Suite Teardown   Tear Down System Test


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
# The DEBUG variable can be overridden to True from run_robot.py by specifying -D
${DEBUG}   False
# Overridden by the bench file (-b); hosted is the safe default
${BENCH_NAME}   hosted
${HAS_HARDWARE}   False
