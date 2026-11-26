import json
import sys
import time

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


SERVICES = [
    {
        "serviceId": 1,
        "channelNumber": 1,
        "name": "Channel 1",
        "description": "Test service",
        "streamProfile": {
            "protocol": "RTP",
            "container": "MPEG-TS",
            "videoCodec": "H.264",
            "audioCodec": "AAC",
            "rtpAddress": "127.0.0.1",
            "rtpPort": 5004
        }
    }
]


class FaultHandler(BaseHTTPRequestHandler):

    def send_response_body(self, status, body):
        if isinstance(body, str):
            data = body.encode()
        else:
            data = json.dumps(body).encode()

        try:
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        mode = self.path.split("/")[1]

        if mode == "timeout":
            time.sleep(1)
            self.send_response_body(200, SERVICES)

        elif mode == "unavailable":
            self.send_response_body(503, {"error": "Unavailable"})

        elif mode == "notfound":
            self.send_response_body(404, {"error": "Not found"})

        elif mode == "invalidjson":
            self.send_response_body(200, "{invalid json")

        elif mode == "invalidschema":
            self.send_response_body(200, [{"serviceId": 1}])

        elif mode == "recovery":
            self.server.recovery_attempts += 1

            if self.server.recovery_attempts <= 2:
                self.send_response_body(
                    503, {"error": "Temporary failure"}
                )
            else:
                self.send_response_body(200, SERVICES)

        elif mode == "cache":
            self.server.cache_attempts += 1

            if self.server.cache_attempts == 2:
                self.send_response_body(200, "{invalid json")
            else:
                self.send_response_body(200, SERVICES)

        else:
            self.send_response_body(200, SERVICES)

    def log_message(self, format, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0

    server = ThreadingHTTPServer(
        ("127.0.0.1", port),
        FaultHandler
    )

    server.recovery_attempts = 0
    server.cache_attempts = 0

    print(server.server_address[1], flush=True)

    server.serve_forever()
