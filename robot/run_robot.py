#!/usr/bin/env python3
"""Common runner for the F769 Robot suite.

Wraps `python -m robot` so local runs and CI pass the same variables:
the bench file, the tag sets (also exposed to tests as ${INCLUDE_TAGS} and
${EXCLUDE_TAGS}, so suites can pick reliability iterations by cadence tag),
the repository root and the debug flag. Output goes to
robot/output/<timestamp>/ (log.html, report.html, output.xml).

Examples:
  python robot/run_robot.py -i premerge -e "needs*"
  python robot/run_robot.py -b robot/benches/dell.yaml -i nightly -D
  python robot/run_robot.py -i premerge -V GATEWAY_DIR:/tmp/gateway -s 10_Build_Tests
"""
import argparse
import datetime
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
SUITES = HERE / "suites" / "F769_Test_Suite"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("-i", "--include", action="append", default=[], help="only run tests matching this tag pattern")
    p.add_argument("-e", "--exclude", action="append", default=[], help="skip tests matching this tag pattern")
    p.add_argument("-V", "--variable", action="append", default=[], help="override a variable, NAME:value")
    p.add_argument("-b", "--bench", default=str(HERE / "benches" / "hosted.yaml"), help="bench variable file")
    p.add_argument("-s", "--suite", action="append", default=[], help="run only suites with this name")
    p.add_argument("-D", "--debug", action="store_true", help="sets ${DEBUG} to True")
    p.add_argument("-o", "--outputdir", help="default: robot/output/<timestamp>")
    a = p.parse_args()

    outdir = Path(a.outputdir) if a.outputdir else HERE / "output" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    cmd = [sys.executable, "-m", "robot",
           "--outputdir", str(outdir),
           "--variablefile", str(Path(a.bench).resolve()),
           "--variable", f"REPO_ROOT:{REPO_ROOT.as_posix()}",
           "--variable", f"INCLUDE_TAGS:{' '.join(a.include)}",
           "--variable", f"EXCLUDE_TAGS:{' '.join(a.exclude)}",
           "--variable", f"DEBUG:{a.debug}"]
    for tag in a.include:
        cmd += ["--include", tag]
    for tag in a.exclude:
        cmd += ["--exclude", tag]
    for var in a.variable:
        cmd += ["--variable", var]
    for suite in a.suite:
        cmd += ["--suite", suite]
    cmd.append(str(SUITES))
    print("Running:", " ".join(cmd), flush=True)
    return subprocess.call(cmd)


if __name__ == "__main__":
    sys.exit(main())
