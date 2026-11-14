"""Local PCM loopback for testing the board, without a speech provider."""

import argparse
import logging
import socketserver

MAX_AUDIO = 16_000 * 2 * 16
DIRECTIONS = {b"WV1 ES RU\n": "ES -> RU", b"WV1 RU ES\n": "RU -> ES"}


class Gateway(socketserver.StreamRequestHandler):
    def handle(self):
        self.connection.settimeout(20)
        try:
            direction = DIRECTIONS.get(self.rfile.readline(11))
            if direction is None:
                logging.warning("Invalid stream header")
                return

            # Replace this receive loop with a streaming speech provider adapter.
            pcm = bytearray()
            while chunk := self.rfile.read1(4096):
                pcm.extend(chunk)
                if len(pcm) > MAX_AUDIO:
                    logging.warning("Capture exceeded the limit")
                    return

            if not pcm or len(pcm) % 2:
                logging.warning("Invalid PCM length")
                return
            logging.info("%s: looping back %.2f s of PCM", direction, len(pcm) / 32000)
            for offset in range(0, len(pcm), 640):
                self.wfile.write(pcm[offset:offset + 640])
        except OSError as error:
            logging.warning("Connection ended: %s", error)


class Server(socketserver.TCPServer):
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO)
    with Server((args.bind, args.port), Gateway) as server:
        logging.info("PCM loopback listening on %s:%d", args.bind, args.port)
        server.serve_forever()


if __name__ == "__main__":
    main()
