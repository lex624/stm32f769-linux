"""Robot keywords for the six-byte control frame and frame-sink's HTTP contract.

Uses only the standard library (urllib, socket, json) so the same keywords
work on GitHub-hosted runners and on the bench, against a host-built
frame-sink or the board's.
"""
import json
import socket
import urllib.error
import urllib.request
from pathlib import Path

from robot.api.deco import keyword, library

VECTORS = Path(__file__).resolve().parents[1] / "data" / "control_frames.json"


@library(scope="GLOBAL", auto_keywords=False)
class ControlFrameLibrary:

    @keyword
    def load_control_frame_vectors(self):
        """Returns the shared vector file as a dictionary with `valid` and `invalid` lists."""
        return json.loads(VECTORS.read_text(encoding="utf-8"))

    @keyword
    def get_free_tcp_port(self):
        """Returns a TCP port that is free on 127.0.0.1 right now."""
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            return s.getsockname()[1]

    @keyword
    def wait_for_http_port(self, port, timeout=10):
        """Waits until something accepts TCP connections on 127.0.0.1:port."""
        import time
        end = time.monotonic() + float(timeout)
        while time.monotonic() < end:
            try:
                with socket.create_connection(("127.0.0.1", int(port)), timeout=0.5):
                    return
            except OSError:
                time.sleep(0.1)
        raise AssertionError(f"Nothing listening on 127.0.0.1:{port} after {timeout} s")

    @keyword
    def post_control_frame(self, base_url, frame_hex):
        """POSTs the frame bytes to <base_url>/control and returns the HTTP status code."""
        req = urllib.request.Request(base_url.rstrip("/") + "/control", data=bytes.fromhex(frame_hex),
                                     headers={"Content-Type": "application/octet-stream"}, method="POST")
        return self._status(req)

    @keyword
    def get_control_state(self, base_url):
        """GETs <base_url>/control and returns its fields (count, sequence, throttle,
        steering, age_ms) as a dictionary of integers."""
        with urllib.request.urlopen(base_url.rstrip("/") + "/control", timeout=5) as r:
            text = r.read().decode()
        return {k: int(v) for k, v in (field.split("=", 1) for field in text.split())}

    @keyword
    def send_preflight(self, base_url, path="/control"):
        """Sends a Chrome-style CORS preflight and returns (status, headers dict)."""
        req = urllib.request.Request(base_url.rstrip("/") + path, method="OPTIONS", headers={
            "Origin": "http://localhost:5173", "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Private-Network": "true"})
        with urllib.request.urlopen(req, timeout=5) as r:
            return r.status, {k.lower(): v for k, v in r.headers.items()}

    @staticmethod
    def _status(req):
        try:
            with urllib.request.urlopen(req, timeout=5) as r:
                return r.status
        except urllib.error.HTTPError as e:
            return e.code
