*** Settings ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Documentation   Build and static checks that need no hardware. These run on every
...             push and pull request (premerge) on GitHub-hosted runners.
Resource   ../../resources/common.robot
Test Tags   critical   premerge   postmerge   nightly   weekend
Test Timeout   15 minutes
Suite Setup   Suite Set Up For Build Testing


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Suite Set Up For Build Testing
   # The gateway is the only build in Phase 0; check its toolchain is present
   # once here, not as a failure inside every test.
   Directory Should Exist   ${GATEWAY_DIR}
   ${node}=   Run Process   node --version   shell=True
   IF   ${node.rc} != 0
      Fatal Error   Node.js is not installed on this runner. Bailing...
   END
   Log   Node.js ${node.stdout}   console=True
   # Install exactly what package-lock.json pins, as CI and releases would
   Run Build Step   npm ci --no-audit --no-fund   ${GATEWAY_DIR}


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Test Cases ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
BLD-01 GATEWAY UNIT TESTS PASS
   [Documentation]   The gateway's node:test suite (control decoding, frame encoding,
   ...               board URLs and the frame send loop) passes with no failures.
   ${result}=   Run Build Step   npm test   ${GATEWAY_DIR}
   # node --test prints a summary ("ℹ pass 6" or "# pass 6" depending on the
   # reporter); make sure tests actually ran, not just that npm returned 0
   Should Match Regexp   ${result.stdout}   (?m)\\bpass [1-9]\\d*\\s*$
   Should Match Regexp   ${result.stdout}   (?m)\\bfail 0\\s*$

BLD-02 GATEWAY BUNDLE BUILDS
   [Documentation]   The gateway page bundles with Vite into gateway/dist, which is
   ...               what the phone loads when it is served over HTTPS.
   Run Build Step   npm run build   ${GATEWAY_DIR}
   File Should Exist   ${GATEWAY_DIR}/dist/index.html
