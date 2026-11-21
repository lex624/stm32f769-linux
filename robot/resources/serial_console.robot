*** Settings ***
Documentation   Generic serial console keywords built on SerialLibrary. Device adapters
...             (resources/devices/*.robot) use these with their own prompts.
...
...             What these keywords work around, found on the STM32F769 bench:
...             - SerialLibrary's default encoding is hexlify, so text consoles import it
...               with iso-8859-1 (every byte decodes; kernel logs are ASCII anyway).
...             - Its default inter_byte_timeout of 0 makes Read Until return empty
...               on Windows as soon as no byte is waiting, so ports open with a
...               non-zero inter-byte timeout and waits loop until their deadline.
...             - The board's UART drops characters when a whole line arrives at
...               once, so commands are typed a character at a time.
...             - Stale prompts in the buffer can satisfy a prompt match, so commands
...               end with a unique marker that only real output can produce.
Library   SerialLibrary   encoding=iso-8859-1
Library   String
Library   DateTime


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Variables ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
# Supplied by the bench file (robot/benches/<bench>.yaml)
${CONSOLE_PORT}   ${EMPTY}
${CONSOLE_BAUD}   115200
# Timeout for one console command; boot waits pass their own
${CONSOLE_TIMEOUT}   30
# Port read timeouts; waits repeat reads until their own deadline
${CONSOLE_READ_TIMEOUT}   2
${CONSOLE_INTER_BYTE_TIMEOUT}   2
# Delay between typed characters, so the target's UART keeps up
${CONSOLE_CHAR_DELAY}   10ms
# Set by Open Device Console, so teardowns only touch a console that was opened
${CONSOLE_IS_OPEN}   ${False}
${CONSOLE_MARKER_COUNT}   ${0}


#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
*** Keywords ***
#~~~~~~~~~~~~~~~~~~~~~~~~~~~#
Open Device Console
   [Documentation]   Opens the bench's console port. Fails the suite straight away when
   ...               the port is missing or held by another program (a terminal left open).
   ${ports}=   List Com Port Names
   IF   '${CONSOLE_PORT}' not in $ports
      Fatal Error   Console port ${CONSOLE_PORT} not found (available: ${ports}). Bailing...
   END
   Add Port   ${CONSOLE_PORT}   baudrate=${CONSOLE_BAUD}   timeout=${CONSOLE_READ_TIMEOUT}
   ...   inter_byte_timeout=${CONSOLE_INTER_BYTE_TIMEOUT}   make_current=True
   Set Global Variable   ${CONSOLE_IS_OPEN}   ${True}
   Log   Console open on ${CONSOLE_PORT} at ${CONSOLE_BAUD} baud   console=True

Close Device Console
   Delete All Ports
   Set Global Variable   ${CONSOLE_IS_OPEN}   ${False}

Wait For Console Text
   [Documentation]   Reads the console until ${text} appears or ${timeout} seconds pass,
   ...               and returns everything read. Fails with the tail of the output when
   ...               the text never arrives.
   [Arguments]   ${text}   ${timeout}=${CONSOLE_TIMEOUT}
   ${deadline}=   Evaluate   time.monotonic() + float($timeout)
   ${output}=   Set Variable   ${EMPTY}
   WHILE   $text not in $output and time.monotonic() < $deadline   limit=NONE
      ${chunk}=   Read Until   expected=${text}
      ${output}=   Set Variable   ${output}${chunk}
   END
   Log   ${output}
   IF   $text not in $output
      ${tail}=   Get Substring   ${output}   -600
      Fail   "${text}" did not appear on ${CONSOLE_PORT} within ${timeout} s. Last output:${\n}${tail}
   END
   RETURN   ${output}

Console Text Appears
   [Documentation]   Like Wait For Console Text but returns True or False instead of failing.
   [Arguments]   ${text}   ${timeout}=${CONSOLE_TIMEOUT}
   ${found}=   Run Keyword And Return Status   Wait For Console Text   ${text}   ${timeout}
   RETURN   ${found}

Send Console Line
   [Documentation]   Types one line a character at a time, then Enter. Does not wait.
   [Arguments]   ${line}
   @{characters}=   Split String To Characters   ${line}${\n}
   FOR   ${character}   IN   @{characters}
      Write Data   ${character}
      Sleep   ${CONSOLE_CHAR_DELAY}
   END

Drain Console
   [Documentation]   Discards everything the target has sent so far, waiting briefly for
   ...               output that is still arriving (echoes, prompts, kernel messages).
   Sleep   200ms
   Reset Input Buffer

Wait For Console Quiet
   [Documentation]   Waits until the target sends nothing for ${quiet} seconds (boot
   ...               messages, kernel warnings), so commands are not typed into a flood
   ...               of output. Logs what arrived meanwhile and returns it.
   [Arguments]   ${quiet}=3   ${timeout}=120
   ${deadline}=   Evaluate   time.monotonic() + float($timeout)
   ${output}=   Set Variable   ${EMPTY}
   Set Port Parameter   timeout   ${quiet}
   WHILE   time.monotonic() < $deadline   limit=NONE
      # Never matches: reads for ${quiet} s, returning early only on a gap
      ${chunk}=   Read Until   expected=\x00\x00quiet\x00\x00
      ${output}=   Set Variable   ${output}${chunk}
      IF   $chunk == ''   BREAK
   END
   Set Port Parameter   timeout   ${CONSOLE_READ_TIMEOUT}
   Log   ${output}
   IF   $output.strip() == '' or time.monotonic() < $deadline
      RETURN   ${output}
   END
   Fail   The console never went quiet for ${quiet} s within ${timeout} s

Run Console Command
   [Documentation]   Runs ${command} and returns its output. The command is followed by
   ...               echo of a unique marker written as two quoted halves: the echoed
   ...               command line shows the halves, only real output shows the joined
   ...               marker, so stale prompts in the buffer cannot end the wait early.
   ...               ${prompt} is then awaited so the target is ready for the next command.
   [Arguments]   ${command}   ${prompt}   ${timeout}=${CONSOLE_TIMEOUT}
   ${count}=   Evaluate   ${CONSOLE_MARKER_COUNT} + 1
   Set Global Variable   ${CONSOLE_MARKER_COUNT}   ${count}
   ${marker}=   Set Variable   __RC_${count}__
   Drain Console
   Send Console Line   ${command}; echo "__RC""_${count}__"
   ${output}=   Wait For Console Text   ${marker}   ${timeout}
   Wait For Console Text   ${prompt}   ${CONSOLE_TIMEOUT}
   # Keep the lines between the echoed command and the marker
   ${output}=   Fetch From Left   ${output}   ${marker}
   ${lines}=   Split To Lines   ${output}
   ${body}=   Evaluate   '\\n'.join(line.rstrip('\\r') for line in $lines if '__RC""' not in line).strip()
   RETURN   ${body}
