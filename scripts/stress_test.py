#!/usr/bin/env python3
import argparse
import json
import selectors
import socket
import struct
import time

LOGIN = 1
BROADCAST = 3
HEARTBEAT = 5


def pack(msg_type, request_id, payload):
    body = json.dumps(payload, separators=(",", ":")).encode()
    return struct.pack("!IHI", len(body), msg_type, request_id) + body


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--connections", type=int, default=3000)
    parser.add_argument("--messages", type=int, default=100000)
    args = parser.parse_args()

    selector = selectors.DefaultSelector()
    sockets = []
    start = time.time()

    for i in range(args.connections):
        sock = socket.create_connection((args.host, args.port), timeout=5)
        sock.setblocking(False)
        selector.register(sock, selectors.EVENT_READ)
        sockets.append(sock)

    for i, sock in enumerate(sockets):
        sock.sendall(pack(LOGIN, 1, {"username": f"user{i}"}))

    sent = 0
    failed = 0
    next_heartbeat = time.time() + 30
    while sent < args.messages:
        sock = sockets[sent % len(sockets)]
        try:
            sock.sendall(pack(BROADCAST, sent + 2, {"message": f"msg-{sent}"}))
            sent += 1
        except (BlockingIOError, BrokenPipeError, ConnectionResetError):
            failed += 1

        now = time.time()
        if now >= next_heartbeat:
            for sock in sockets:
                try:
                    sock.sendall(pack(HEARTBEAT, sent + 2, {}))
                except OSError:
                    failed += 1
            next_heartbeat = now + 30

        for key, _ in selector.select(timeout=0):
            try:
                key.fileobj.recv(65536)
            except OSError:
                failed += 1

    elapsed = time.time() - start
    for sock in sockets:
        selector.unregister(sock)
        sock.close()

    print(f"connections={args.connections} messages={sent} failed={failed} elapsed={elapsed:.2f}s")


if __name__ == "__main__":
    main()
