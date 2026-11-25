#!/usr/bin/env python3

import argparse
from functools import partial
from http.server import (
    SimpleHTTPRequestHandler,
    ThreadingHTTPServer
)
from pathlib import Path
from urllib.parse import urlsplit


PROJECT_ROOT = Path(__file__).resolve().parent.parent


class HlsRequestHandler(SimpleHTTPRequestHandler):

    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".m3u8": "application/vnd.apple.mpegurl",
        ".ts": "video/mp2t",
        ".js": "application/javascript",
        ".css": "text/css",
        ".html": "text/html",
        ".json": "application/json"
    }

    def end_headers(self):

        path = urlsplit(self.path).path

        if path.endswith((".m3u8", ".ts")):
            self.send_header(
                "Cache-Control",
                "no-store, no-cache, must-revalidate"
            )

        super().end_headers()


def main():

    parser = argparse.ArgumentParser(
        description="IPTV M10 Prototype HTTP Server"
    )

    parser.add_argument(
        "--bind",
        default="127.0.0.1"
    )

    parser.add_argument(
        "--port",
        type=int,
        default=8081
    )

    args = parser.parse_args()

    handler = partial(
        HlsRequestHandler,
        directory=str(PROJECT_ROOT)
    )

    with ThreadingHTTPServer(
        (args.bind, args.port),
        handler
    ) as server:

        print("IPTV M10 Prototype HTTP Server")
        print(f"Project root: {PROJECT_ROOT}")
        print(f"HTTP address: {args.bind}")
        print(f"HTTP port: {args.port}")
        print("HLS path: /runtime/hls/index.m3u8")
        print("Frontend path: /frontend/")
        print("Press Ctrl+C to stop")

        try:
            server.serve_forever()

        except KeyboardInterrupt:
            print("\nHTTP server stopped")


if __name__ == "__main__":
    main()
