#!/usr/bin/env python3
import json
import socket
import struct
import subprocess
import sys
import time

LOGIN = 1
LOGOUT = 2
BROADCAST = 3
PRIVATE = 4
HEARTBEAT = 5
ACK = 6
ERROR = 7


def pack(msg_type, request_id, payload):
    body = json.dumps(payload, separators=(",", ":")).encode()
    return struct.pack("!IHI", len(body), msg_type, request_id) + body


def recv_one(sock):
    header = sock.recv(10)
    if len(header) != 10:
        raise RuntimeError("short header")
    length, msg_type, request_id = struct.unpack("!IHI", header)
    body = b""
    while len(body) < length:
        chunk = sock.recv(length - len(body))
        if not chunk:
            raise RuntimeError("short body")
        body += chunk
    return msg_type, request_id, json.loads(body.decode())


def recv_until(sock, predicate, limit=10):
    for _ in range(limit):
        item = recv_one(sock)
        if predicate(item):
            return item
    raise AssertionError("expected message was not received")


def connect_user(name, port):
    sock = socket.create_connection(("127.0.0.1", port), timeout=3)
    sock.sendall(pack(LOGIN, 1, {"username": name}))
    msg_type, _, payload = recv_until(sock, lambda item: item[0] == ACK)
    assert msg_type == ACK, payload
    return sock


def main():
    port = 19000
    server = subprocess.Popen(
        ["./build/chat_server", "--host", "127.0.0.1", "--port", str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        time.sleep(0.5)
        alice = connect_user("alice", port)
        bob = connect_user("bob", port)

        alice.sendall(pack(BROADCAST, 2, {"message": "hello"}))
        recv_until(alice, lambda item: item[0] == BROADCAST and item[2].get("message") == "hello")
        recv_until(bob, lambda item: item[0] == BROADCAST and item[2].get("message") == "hello")

        alice.sendall(pack(PRIVATE, 3, {"to": "bob", "message": "secret"}))
        private = recv_until(bob, lambda item: item[0] == PRIVATE and item[2].get("message") == "secret")
        assert private[0] == PRIVATE

        dup = socket.create_connection(("127.0.0.1", port), timeout=3)
        dup.sendall(pack(LOGIN, 4, {"username": "alice"}))
        err = recv_one(dup)
        assert err[0] == ERROR

        alice.sendall(pack(HEARTBEAT, 5, {}))
        assert recv_one(alice)[0] == ACK

        alice.sendall(pack(LOGOUT, 6, {}))
        bob.close()
        dup.close()
        alice.close()
        print("functional tests passed")
    finally:
        server.terminate()
        server.wait(timeout=3)


if __name__ == "__main__":
    sys.exit(main())
