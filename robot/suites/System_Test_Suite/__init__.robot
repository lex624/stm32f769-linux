*** Settings ***
Documentation   Device system test: build, install, platform, connectivity, USB host,
...             control interface, telemetry and LiveKit end-to-end tests. The same
...             suites run on every device family; see robot/PLAN.md.
# Variables and keywords defined here would not reach child suites, so shared
# ones live in common.robot, which every suite also imports.
Resource   ../../resources/common.robot
Suite Setup   Initialise System Test
Suite Teardown   Tear Down System Test
