*** Settings ***
Documentation   The six-byte control frame and frame-sink's HTTP contract, built on
...             RequestsLibrary. Works against a host-built frame-sink or a board's.
Library   RequestsLibrary
Library   OperatingSystem
Library   Collections


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
${CONTROL_FRAME_VECTORS}   ${CURDIR}/../data/control_frames.json
# Headers Chrome sends for the gateway page's preflight
&{PREFLIGHT_HEADERS}   Origin=http://localhost:5173   Access-Control-Request-Method=POST
...   Access-Control-Request-Private-Network=true


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Load Control Frame Vectors
   [Documentation]   Returns the shared vector file as a dictionary with `valid` and
   ...               `invalid` lists.
   ${text}=   Get File   ${CONTROL_FRAME_VECTORS}
   ${vectors}=   Evaluate   json.loads($text)
   RETURN   ${vectors}

Get Free Local Port
   [Documentation]   A TCP port that is free on 127.0.0.1 right now.
   ${port}=   Evaluate   (lambda s: (s.bind(('127.0.0.1', 0)), s.getsockname()[1], s.close())[1])(socket.socket())
   RETURN   ${port}

Frame Sink Should Answer
   [Arguments]   ${base_url}
   GET   ${base_url}/control   expected_status=200   timeout=1

Wait For Frame Sink
   [Documentation]   Waits until frame-sink answers GET /control at ${base_url}.
   [Arguments]   ${base_url}   ${timeout}=10s
   Wait Until Keyword Succeeds   ${timeout}   200ms   Frame Sink Should Answer   ${base_url}

Post Control Frame
   [Documentation]   POSTs the frame bytes (hex) to /control and returns the status code.
   [Arguments]   ${base_url}   ${frame_hex}
   ${body}=   Evaluate   bytes.fromhex($frame_hex)
   &{headers}=   Create Dictionary   Content-Type=application/octet-stream
   ${response}=   POST   ${base_url}/control   data=${body}   headers=${headers}   expected_status=any   timeout=5
   RETURN   ${response.status_code}

Get Control State
   [Documentation]   GET /control, returned as a dictionary of integers: count, sequence,
   ...               throttle, steering and age_ms.
   [Arguments]   ${base_url}
   ${response}=   GET   ${base_url}/control   expected_status=200   timeout=5
   ${state}=   Evaluate   {k: int(v) for k, v in (f.split('=', 1) for f in $response.text.split())}
   RETURN   ${state}

Send Control Preflight
   [Documentation]   Sends Chrome's CORS and Private Network Access preflight and returns
   ...               the response (status_code, headers).
   [Arguments]   ${base_url}
   ${response}=   OPTIONS   ${base_url}/control   headers=${PREFLIGHT_HEADERS}   expected_status=any   timeout=5
   RETURN   ${response}
