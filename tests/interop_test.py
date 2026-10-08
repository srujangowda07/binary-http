#!/usr/bin/env python3
"""
Independent interoperability test for BHTTP protocol.
This script builds and inspects raw binary frames directly from SPEC.md
to test the server independently of the bcurl client.
"""

import socket
import struct
import sys
import os

STATIC_TABLE = {
    ":method": 1,
    ":path": 2,
    ":status": 3,
    "content-length": 4,
    "content-type": 5,
    "host": 6,
    "user-agent": 7,
    "server": 8,
    "accept": 9,
    "connection": 10
}

REV_STATIC_TABLE = {v: k for k, v in STATIC_TABLE.items()}

def encode_header(name: str, value: str) -> bytes:
    val_bytes = value.encode('utf-8')
    name_lower = name.lower()
    if name_lower in STATIC_TABLE:
        hdr_id = STATIC_TABLE[name_lower]
        return struct.pack("!BH", hdr_id, len(val_bytes)) + val_bytes
    else:
        name_bytes = name.encode('utf-8')
        return struct.pack("!BH", 0, len(name_bytes)) + name_bytes + struct.pack("!H", len(val_bytes)) + val_bytes

def build_request_frame(stream_id: int, method: str, path: str, extra_headers: list = None) -> bytes:
    headers = [(":method", method), (":path", path), ("host", "localhost:9000"), ("user-agent", "interop-test/1.0")]
    if extra_headers:
        headers.extend(extra_headers)

    payload = struct.pack("!H", len(headers))
    for name, val in headers:
        payload += encode_header(name, val)

    length = len(payload)
    hdr = struct.pack("!3sBB4s",
                      length.to_bytes(3, 'big'),
                      0x01, # FRAME_REQUEST
                      0x01, # FLAG_END_STREAM
                      (stream_id & 0x7FFFFFFF).to_bytes(4, 'big'))
    return hdr + payload

def parse_response_frame(sock: socket.socket):
    hdr_bytes = sock.recv(9)
    if len(hdr_bytes) < 9:
        raise ConnectionError(f"Incomplete frame header: received {len(hdr_bytes)} bytes")

    length = int.from_bytes(hdr_bytes[0:3], 'big')
    frame_type = hdr_bytes[3]
    flags = hdr_bytes[4]
    stream_id = int.from_bytes(hdr_bytes[5:9], 'big') & 0x7FFFFFFF

    # Read exactly length bytes
    payload = b""
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            raise ConnectionError("Connection closed while reading payload")
        payload += chunk

    if frame_type != 0x02:
        return {"type": frame_type, "length": length, "stream_id": stream_id, "payload": payload}

    # Decode response
    num_headers = struct.unpack("!H", payload[0:2])[0]
    offset = 2
    headers = {}
    for _ in range(num_headers):
        hdr_id = payload[offset]
        offset += 1
        if hdr_id in REV_STATIC_TABLE:
            hname = REV_STATIC_TABLE[hdr_id]
            vlen = struct.unpack("!H", payload[offset:offset+2])[0]
            offset += 2
            vbytes = payload[offset:offset+vlen]
            offset += vlen
            headers[hname] = vbytes.decode('utf-8', errors='replace')
        elif hdr_id == 0:
            nlen = struct.unpack("!H", payload[offset:offset+2])[0]
            offset += 2
            nbytes = payload[offset:offset+nlen]
            offset += nlen
            vlen = struct.unpack("!H", payload[offset:offset+2])[0]
            offset += 2
            vbytes = payload[offset:offset+vlen]
            offset += vlen
            headers[nbytes.decode('utf-8', errors='replace')] = vbytes.decode('utf-8', errors='replace')

    body = payload[offset:]
    status = int(headers.get(":status", "0"))
    return {
        "type": frame_type,
        "flags": flags,
        "stream_id": stream_id,
        "status": status,
        "headers": headers,
        "body": body
    }

def main():
    host = "127.0.0.1"
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 9000
    print(f"[*] Running independent BHTTP interoperability tests against {host}:{port}")

    # Test 1: Basic GET /index.html
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    req = build_request_frame(stream_id=1, method="GET", path="/index.html")
    s.sendall(req)
    res = parse_response_frame(s)
    assert res["status"] == 200, f"Expected 200, got {res['status']}"
    assert res["stream_id"] == 1, f"Expected stream 1, got {res['stream_id']}"
    assert b"Hello, Binary HTTP!" in res["body"], "Expected body to contain 'Hello, Binary HTTP!'"
    print("  [PASS] Test 1: GET /index.html returned 200 OK with correct body")
    s.close()

    # Test 2: Missing file /nonexistent.txt -> 404
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    req = build_request_frame(stream_id=1, method="GET", path="/nonexistent.txt")
    s.sendall(req)
    res = parse_response_frame(s)
    assert res["status"] == 404, f"Expected 404, got {res['status']}"
    print("  [PASS] Test 2: Missing file returned 404 Not Found")
    s.close()

    # Test 3: Path traversal attempt -> 400
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    req = build_request_frame(stream_id=1, method="GET", path="/../../etc/passwd")
    s.sendall(req)
    res = parse_response_frame(s)
    assert res["status"] == 400, f"Expected 400, got {res['status']}"
    print("  [PASS] Test 3: Path traversal rejected with 400 Bad Request")
    s.close()

    # Test 4: Unknown frame skipping per extensibility requirement
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    # Send unknown frame type 0xAA with 16 bytes of payload
    unknown_len = 16
    unknown_payload = b"X" * unknown_len
    unknown_hdr = struct.pack("!3sBB4s",
                              unknown_len.to_bytes(3, 'big'),
                              0xAA, # Unknown type
                              0x00,
                              (99).to_bytes(4, 'big'))
    s.sendall(unknown_hdr + unknown_payload)

    # Immediately follow on the same connection with valid GET /hello.txt
    req = build_request_frame(stream_id=2, method="GET", path="/hello.txt")
    s.sendall(req)

    res = parse_response_frame(s)
    assert res["status"] == 200, f"Expected 200 after unknown frame, got {res['status']}"
    assert res["stream_id"] == 2, f"Expected stream 2, got {res['stream_id']}"
    assert b"Hello, BHTTP!" in res["body"], "Expected body from /hello.txt"
    print("  [PASS] Test 4: Unknown frame (0xAA) skipped cleanly, followed by valid 200 OK")
    s.close()

    # Test 5: Persistent connection with sequential multi-request exchanges
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))

    req1 = build_request_frame(stream_id=1, method="GET", path="/index.html")
    s.sendall(req1)
    res1 = parse_response_frame(s)
    assert res1["status"] == 200 and res1["stream_id"] == 1

    req2 = build_request_frame(stream_id=2, method="GET", path="/hello.txt")
    s.sendall(req2)
    res2 = parse_response_frame(s)
    assert res2["status"] == 200 and res2["stream_id"] == 2

    req3 = build_request_frame(stream_id=3, method="GET", path="/sample.json")
    s.sendall(req3)
    res3 = parse_response_frame(s)
    assert res3["status"] == 200 and res3["stream_id"] == 3
    assert b"binary-http" in res3["body"]

    print("  [PASS] Test 5: Persistent connection served 3 sequential exchanges on one TCP socket")
    s.close()

    # Test 6: Custom literal header handling
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    req = build_request_frame(stream_id=1, method="GET", path="/index.html", extra_headers=[("x-custom-test", "interop-val")])
    s.sendall(req)
    res = parse_response_frame(s)
    assert res["status"] == 200
    print("  [PASS] Test 6: Request with custom literal header processed successfully")
    s.close()

    print("\n[SUCCESS] All independent interoperability tests passed!\n")

if __name__ == "__main__":
    main()
