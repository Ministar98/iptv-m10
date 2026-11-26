import select
import subprocess
import sys


def main():
    if len(sys.argv) != 3:
        print("Usage: runner SERVER_SCRIPT TEST_EXECUTABLE")
        return 1

    server = subprocess.Popen(
        [sys.executable, sys.argv[1], "0"],
        stdout=subprocess.PIPE,
        text=True
    )

    try:
        ready, _, _ = select.select(
            [server.stdout], [], [], 5
        )

        if not ready:
            raise RuntimeError("Test server startup timeout")

        port = int(server.stdout.readline().strip())

        result = subprocess.run(
            [
                sys.argv[2],
                f"http://127.0.0.1:{port}"
            ],
            timeout=15
        )

        return result.returncode

    finally:
        server.terminate()

        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()

        server.stdout.close()


if __name__ == "__main__":
    sys.exit(main())
