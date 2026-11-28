#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Stefan Reinauer
"""Exercise the production download recipes against local HTTP failures."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = b"verified archive fixture\n"
DIGEST = hashlib.md5(PAYLOAD).hexdigest()
requests = []


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append(self.path)
        if self.path.startswith("/http-error/"):
            status, body = 504, b"<html>504 Gateway Time-out</html>"
        elif self.path.startswith("/bad-checksum/"):
            status, body = 200, b"unexpected archive contents"
        else:
            status, body = 200, PAYLOAD
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
base = "http://127.0.0.1:" + str(server.server_port)

try:
    with tempfile.TemporaryDirectory(prefix="xsysinfo-downloads-") as tmp:
        downloads = Path(tmp) / "downloads"

        def run(filename, mirrors, force=False):
            target = downloads / filename
            command = ["make", "--no-print-directory", "-f", str(ROOT / "Makefile"),
                       "DOWNLOAD_DIR=" + str(downloads),
                       "AMINET_MIRRORS=" + " ".join(base + m for m in mirrors),
                       "OPENPCI_MD5=" + DIGEST, "MMULIB_MD5=" + DIGEST,
                       "MU_MANUAL_MD5=" + DIGEST, "NDK_PATH=/unused",
                       "FULL_VERSION=0.0-test", str(target)]
            if force:
                command.insert(1, "-B")
            environment = dict(os.environ, NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")
            result = subprocess.run(command, cwd=ROOT, env=environment, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=30)
            return target, result

        for filename in ("openpci68k.lha", "MMULib.lha", "MuManual.lha"):
            requests.clear()
            target, result = run(filename, ["/http-error", "/bad-checksum", "/good"])
            assert result.returncode == 0, result.stdout
            assert target.read_bytes() == PAYLOAD
            assert not Path(str(target) + ".tmp").exists()
            assert any(p.startswith("/http-error/") for p in requests)
            assert any(p.startswith("/bad-checksum/") for p in requests)
            assert requests[-1].startswith("/good/")
            assert "MD5 mismatch" in result.stdout
            requests.clear()
            _, result = run(filename, ["/http-error"], force=True)
            assert result.returncode == 0, result.stdout
            assert not requests, "verified cached archive was downloaded again"

        target.unlink()
        target, result = run("MuManual.lha", ["/http-error", "/bad-checksum"])
        assert result.returncode != 0, result.stdout
        assert not target.exists()
        assert not Path(str(target) + ".tmp").exists()
finally:
    server.shutdown()
    server.server_close()
    thread.join()

print("HTTP errors, checksum failures, mirror fallback and cached archives passed")
