*** Settings ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Documentation   The six-byte control frame contract shared by the gateway (JS
...             encodeFrame), frame-sink (POST /control) and f769-control. Uses the
...             vectors in robot/data/control_frames.json. With no ${CONTROL_BASE_URL}
...             the tests start a host-built frame-sink; set it to a board's
...             http://<ip>:8090 to run the same tests against the board.
Resource   ../../resources/common.robot
Resource   ../../resources/control_frame.robot
Test Tags   critical   premerge   postmerge   nightly   weekend   cap-http-control
Test Timeout   5 minutes
Suite Setup   Suite Set Up For Control Interface Testing
Suite Teardown   Terminate All Processes   kill=True


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
# Empty: start frame-sink on this host. Set (e.g. by a bench file) to test a board.
${CONTROL_BASE_URL}   ${EMPTY}


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Suite Set Up For Control Interface Testing
   ${vectors}=   Load Control Frame Vectors
   Set Suite Variable   ${VECTORS}   ${vectors}
   Set Suite Variable   ${FRAME_SINK_URL}   ${CONTROL_BASE_URL}

Use Frame Sink
   [Documentation]   Returns the frame-sink base URL, starting a host-built frame-sink
   ...               on a free local port the first time when no board URL is given.
   IF   '${FRAME_SINK_URL}' == ''
      ${binary}=   Build Host Tool   bench/frame-sink.c   frame-sink
      ${port}=   Get Free Local Port
      Start Process   ${binary}   ${port}   127.0.0.1   alias=frame-sink
      ...   stdout=${HOST_BUILD_DIR}/frame-sink.log   stderr=STDOUT
      Set Suite Variable   ${FRAME_SINK_URL}   http://127.0.0.1:${port}
      Wait For Frame Sink   ${FRAME_SINK_URL}
   END
   RETURN   ${FRAME_SINK_URL}

Frame As Bytes File
   [Documentation]   Writes the hex frames to a binary file, for programs reading stdin.
   [Arguments]   ${path}   @{hex_frames}
   ${hex}=   Catenate   SEPARATOR=   @{hex_frames}
   ${bytes}=   Evaluate   bytes.fromhex($hex)
   Evaluate   pathlib.Path($path).write_bytes($bytes)
   RETURN   ${path}


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Test Cases ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
CTL-01 VALID CONTROL FRAMES ARE ACCEPTED AND REPORTED
   [Documentation]   Each valid vector POSTed to /control returns 204, and GET /control
   ...               then reports that frame's sequence, throttle and steering with the
   ...               count one higher. Counts are compared as deltas so a board that
   ...               already received samples passes too.
   ${url}=   Use Frame Sink
   FOR   ${frame}   IN   @{VECTORS}[valid]
      ${before}=   Get Control State   ${url}
      ${status}=   Post Control Frame   ${url}   ${frame}[hex]
      Should Be Equal As Integers   ${status}   204   msg=${frame}[name]: POST /control returned ${status}
      ${after}=   Get Control State   ${url}
      Should Be Equal As Integers   ${after}[count]   ${{ $before['count'] + 1 }}   msg=${frame}[name]: count did not advance
      Should Be Equal As Integers   ${after}[sequence]   ${frame}[sequence]
      Should Be Equal As Integers   ${after}[throttle]   ${frame}[throttle]
      Should Be Equal As Integers   ${after}[steering]   ${frame}[steering]
      Should Be True   0 <= ${after}[age_ms] < 5000
   END

CTL-02 INVALID CONTROL FRAMES ARE REJECTED
   [Documentation]   Wrong version, out-of-range axes and short frames return 400 and
   ...               leave the reported state unchanged.
   ${url}=   Use Frame Sink
   ${before}=   Get Control State   ${url}
   FOR   ${frame}   IN   @{VECTORS}[invalid]
      ${status}=   Post Control Frame   ${url}   ${frame}[hex]
      Run Keyword And Continue On Failure   Should Be Equal As Integers   ${status}   400
      ...   msg=${frame}[name]: expected 400, got ${status}
   END
   ${after}=   Get Control State   ${url}
   Should Be Equal As Integers   ${after}[count]   ${before}[count]   msg=a rejected frame was counted

CTL-03 CONTROL PREFLIGHT ALLOWS THE GATEWAY PAGE
   [Documentation]   Chrome's CORS and Private Network Access preflight succeeds, and is
   ...               cacheable so it does not double every control request.
   ${url}=   Use Frame Sink
   ${response}=   Send Control Preflight   ${url}
   Should Be Equal As Integers   ${response.status_code}   204
   # requests' headers are case-insensitive
   Should Be Equal   ${response.headers}[Access-Control-Allow-Origin]   *
   Should Be Equal   ${response.headers}[Access-Control-Allow-Private-Network]   true
   Should Contain   ${response.headers}[Access-Control-Allow-Methods]   POST
   Should Be True   int(${response.headers}[Access-Control-Max-Age]) >= 60

CTL-04 GATEWAY ENCODER MATCHES THE SHARED VECTORS
   [Documentation]   The gateway's encodeFrame turns each vector's axes and sequence into
   ...               exactly the vector's bytes.
   Require Tool   node
   ${result}=   Run Process   node   ${REPO_ROOT}/robot/lib/encode_vectors.mjs
   ...   ${GATEWAY_DIR}/controls.js   ${REPO_ROOT}/robot/data/control_frames.json   stderr=STDOUT
   Should Be Equal As Integers   ${result.rc}   0   msg=${result.stdout}
   FOR   ${frame}   IN   @{VECTORS}[valid]
      Should Contain   ${result.stdout}   ${frame}[name]|${frame}[hex]   msg=encodeFrame disagrees for ${frame}[name]:${\n}${result.stdout}
   END

CTL-05 BOARD DECODER MATCHES THE SHARED VECTORS
   [Documentation]   f769-control, the board's stdin decoder, prints each valid vector's
   ...               fields and rejects each invalid one with a non-zero exit.
   ${binary}=   Build Host Tool   port/f769-control.c   f769-control
   @{valid_hex}=   Evaluate   [v['hex'] for v in $VECTORS['valid']]
   ${input}=   Frame As Bytes File   ${HOST_BUILD_DIR}/valid-frames.bin   @{valid_hex}
   ${result}=   Run Process   ${binary}   stdin=${input}
   Should Be Equal As Integers   ${result.rc}   0   msg=${result.stderr}
   FOR   ${frame}   IN   @{VECTORS}[valid]
      Should Contain   ${result.stdout}   sequence=${frame}[sequence] throttle=${frame}[throttle] steering=${frame}[steering]
   END
   FOR   ${frame}   IN   @{VECTORS}[invalid]
      ${input}=   Frame As Bytes File   ${HOST_BUILD_DIR}/invalid-frame.bin   ${frame}[hex]
      ${result}=   Run Process   ${binary}   stdin=${input}
      Run Keyword And Continue On Failure   Should Not Be Equal As Integers   ${result.rc}   0
      ...   msg=${frame}[name] was accepted
   END
