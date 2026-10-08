# Binary HTTP (BHTTP)

> A clean, lightweight binary application-layer network protocol written in C directly over raw TCP sockets.

---

## What is This Project?

**BHTTP** is a custom binary protocol designed to replace the text-based parsing of standard HTTP/1.1 with a fast, deterministic binary format. 

In traditional HTTP/1.1, messages are plain text separated by line breaks (`\r\n\r\n`). Parsing text with string searches and regex is slow and prone to security vulnerabilities (like HTTP request smuggling). 

**BHTTP solves this by making framing binary:**
- Every frame starts with a **fixed 9-byte binary header** that explicitly tells the receiver how many bytes to read.
- Common header names (like `content-type` or `host`) are compressed into **1-byte IDs** using a static lookup table.
- Socket operations cleanly handle TCP stream chunking with helper functions (`read_exact`, `write_all`).
- Connections stay open by default for multiple requests (**persistent connections**).
- Directory traversal attacks (like `../../etc/passwd`) are caught and blocked before touching disk.

The project includes two programs:
1. **`observe`** (also aliased as **`bserve`**): A persistent server that serves files from a local directory (document root).
2. **`bcurl`**: A client tool (similar to `curl`) to send requests, print responses, and inspect raw binary frames on the wire (`-v`).

---

## How It Works (Core Concepts)

### 1. The 9-Byte Binary Frame Header
Every message sent over the wire starts with exactly 9 bytes in **Big-Endian (Network Byte Order)**:

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Length (24 bits)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Type (8 bits)        |         Flags (8 bits)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+---------------+---------------+
|R|                    Stream ID (31 bits)                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

Here is what each field means:
- **Length (3 bytes / 24 bits):** The size of the payload following this header (max ~16 MB). Because it's explicitly 24 bits, the receiver knows exactly how much memory to allocate without guessing.
- **Type (1 byte / 8 bits):** `0x01` for a Request frame, `0x02` for a Response frame.
- **Flags (1 byte / 8 bits):** `0x01` (`FLAG_END_STREAM`) marks that this frame completes the message.
- **Reserved Bit (R):** Set to `0`.
- **Stream ID (31 bits):** A sequence number identifying which transaction this frame belongs to (e.g. Stream 1, Stream 2).

### 2. Static Header Compression Table
Instead of repeatedly typing out full header strings, BHTTP maps the 10 most common headers to single-byte numbers:

| ID | Header Name | ID | Header Name |
| :---: | :--- | :---: | :--- |
| `0x01` | `:method` | `0x06` | `host` |
| `0x02` | `:path` | `0x07` | `user-agent` |
| `0x03` | `:status` | `0x08` | `server` |
| `0x04` | `content-length` | `0x09` | `accept` |
| `0x05` | `content-type` | `0x0A` | `connection` |

- **Known header:** `[1 byte ID][2 bytes value length][value bytes]`  
  *(Example: sending `content-type: text/plain` sends `0x05` + `0x000A` + `"text/plain"`)*
- **Custom / Literal header:** If a header isn't in the table, it uses ID `0x00`:  
  `[0x00][2 bytes name length][name bytes][2 bytes value length][value bytes]`
- Header values are **length-prefixed**, meaning they can safely carry arbitrary binary data without needing null terminators (`\0`).

### 3. Handling Real TCP Streams
TCP delivers an unstructured stream of bytes—not individual messages. A single `send()` call might arrive broken across two `recv()` calls, or multiple messages might get glued together in the network buffer.

BHTTP handles this using dedicated stream helpers in `src/net.c`:
- **`net_read_exact()`**: Loops `recv()` until the exact requested number of bytes have arrived. It automatically handles interrupted system calls (`EINTR`) and detects premature disconnects.
- **`net_write_all()`**: Loops `send()` until the entire buffer is pushed into the socket kernel buffer.
- **`net_discard_exact()`**: If an unknown frame arrives, this safely reads and discards that exact number of bytes so the connection can continue cleanly.

### 4. Persistent Connections
When the server sends a response, it **does not close the socket**. It immediately loops back and listens for the next 9-byte header from the same client. Only when the client disconnects (or times out) does the server close the connection.

### 5. Built-in Security Sandbox
The server validates requested file paths in `resolve_safe_path()`:
- Rejects any URL path containing `..`, backslashes (`\`), or null bytes with `400 Bad Request`.
- Canonicalizes the path using `realpath()` and verifies that the target file strictly lives inside the `./www` folder.

---

## Quick Start & Build Instructions

### Prerequisites
- GCC or Clang (supporting C99)
- GNU Make
- Linux, macOS, or Windows with WSL

---

### Building on Linux / Ubuntu

```bash
# Compile server, client, and unit tests
make all

# Run all automated tests
make test

# Clean compiled binaries and object files
make clean
```

---

### Building & Running on Windows (via WSL)

Because network socket programming uses POSIX APIs (`<sys/socket.h>`), Windows users run the project using **WSL (Windows Subsystem for Linux)**.

> 💡 **Important for Windows PowerShell:**  
> The binaries (`observe`, `bcurl`) are Linux executables. When running them from Windows PowerShell, remember to prefix commands with `wsl` (for example: `wsl ./bcurl ...`).  
> Alternatively, type `wsl` into PowerShell once to switch into the Linux terminal directly.

#### PowerShell One-Liners:
```powershell
# Compile everything
wsl make all

# Run the complete test suite
wsl make test

# Start the server on port 9000
wsl ./observe ./www 9000

# Fetch a file using bcurl
wsl ./bcurl localhost:9000/hello.txt
```

---

## Step-by-Step Manual Testing

To see the client and server communicate, open **two terminal windows**:

### Step 1: Start the Server (Terminal 1)
```bash
# In Linux / WSL:
./observe ./www 9000

# Or from Windows PowerShell:
wsl ./observe ./www 9000
```
You will see:
```text
[observe] Serving ./www on port 9000
```

---

### Step 2: Run Client Requests (Terminal 2)

#### 1. Fetch Plain Text
```bash
wsl ./bcurl localhost:9000/hello.txt
```
**Output:**
```text
Hello, BHTTP!
This is a plain text file served cleanly over the custom binary protocol.
```

#### 2. Fetch JSON
```bash
wsl ./bcurl localhost:9000/sample.json
```
**Output:**
```json
{
  "project": "binary-http",
  "course": "Computer Networks",
  "protocol": "BHTTP",
  "version": "1.0",
  "features": [
    "binary_framing",
    "static_header_table",
    "length_prefixed_values",
    "persistent_connections",
    "unknown_frame_skipping"
  ]
}
```

#### 3. Fetch HTML
```bash
wsl ./bcurl localhost:9000/index.html
```
**Output:**
```html
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>Binary HTTP</title>
</head>
<body>
    <h1>Hello, Binary HTTP!</h1>
    <p>This page was served over a custom binary framing protocol running on raw TCP.</p>
</body>
</html>
```

#### 4. Verbose Mode (`-v` flag)
Adding `-v` prints connection details and an annotated hexdump of the raw binary frames on `stderr`, while keeping `stdout` clean for the body:

```bash
wsl ./bcurl -v localhost:9000/hello.txt
```
**Output:**
```text
* Connecting to localhost port 9000...
* Connected successfully.
> Transmitting FRAME_REQUEST (65 bytes, Stream ID 1):
> 0000  00 00 38 01 01 00 00 00  01 00 05 01 00 03 47 45  |..8...........GE|
> 0010  54 02 00 0a 2f 68 65 6c  6c 6f 2e 74 78 74 06 00  |T.../hello.txt..|
> 0020  0e 6c 6f 63 61 6c 68 6f  73 74 3a 39 30 30 30 07  |.localhost:9000.|
> 0030  00 09 62 63 75 72 6c 2f  31 2e 30 09 00 03 2a 2f  |..bcurl/1.0...*/|
> 0040  2a                                                |*|
< Received FRAME_RESPONSE (Length: 141, Stream ID: 1):
< 0000  00 00 8d 02 01 00 00 00  01 00 05 03 00 03 32 30  |..............20|
< 0010  30 05 00 0a 74 65 78 74  2f 70 6c 61 69 6e 04 00  |0...text/plain..|
< 0020  02 38 38 08 00 0b 6f 62  73 65 72 76 65 2f 31 2e  |.88...observe/1.|
< 0030  30 0a 00 0a 6b 65 65 70  2d 61 6c 69 76 65 48 65  |0...keep-aliveHe|
< 0040  6c 6c 6f 2c 20 42 48 54  54 50 21 0a 54 68 69 73  |llo, BHTTP!.This|
< 0050  20 69 73 20 61 20 70 6c  61 69 6e 20 74 65 78 74  | is a plain text|
< 0060  20 66 69 6c 65 20 73 65  72 76 65 64 20 63 6c 65  | file served cle|
< 0070  61 6e 6c 79 20 6f 76 65  72 20 74 68 65 20 63 75  |anly over the cu|
< 0080  73 74 6f 6d 20 62 69 6e  61 72 79 20 70 72 6f 74  |stom binary prot|
< 0090  6f 63 6f 6c 2e 0a                                 |ocol..|
< Status: 200
< :status: 200
< content-type: text/plain
< content-length: 88
< server: observe/1.0
< connection: keep-alive
Hello, BHTTP!
This is a plain text file served cleanly over the custom binary protocol.
```

---

### Error Handling Tests

#### 1. File Not Found (`404`)
```bash
wsl ./bcurl -v localhost:9000/nonexistent.html
```
**Output:**
```text
* Connecting to localhost port 9000...
* Connected successfully.
...
< Status: 404
< :status: 404
< content-type: text/plain
< content-length: 39
< server: observe/1.0
< connection: keep-alive
404 Not Found: Resource Does Not Exist
```
*(Client exits with code `1`)*

#### 2. Directory Traversal Rejection (`400`)
```bash
wsl ./bcurl -v localhost:9000/../../etc/passwd
```
**Output:**
```text
* Connecting to localhost port 9000...
* Connected successfully.
...
< Status: 400
< :status: 400
< content-type: text/plain
< content-length: 36
< server: observe/1.0
< connection: keep-alive
400 Bad Request: Path Traversal Rejected
```
*(The path is blocked by the security sandbox before accessing disk)*

---

## Automated Test Suite

You can run all tests with one command:
```bash
make test
# OR from Windows PowerShell:
wsl make test
```

### What is Tested:
1. **`tests/test_frames.c`**: Validates 9-byte header packing, bit shifting, and 24-bit length edge cases (`0` to `16,777,215`).
2. **`tests/test_headers.c`**: Validates static table IDs (`1..10`), custom literal header format (`0x00`), and malformed header detection.
3. **`tests/test_protocol.c`**: Compares serialized frames byte-for-byte against the expected wire representation.
4. **`tests/test_integration.sh`**: Boots the server in the background and tests file fetching, 404 responses, traversal rejection, and verbose output separation.
5. **`tests/interop_test.py`**: An independent Python test client that talks directly to the server using raw sockets. It tests:
   - Persistent sequential requests on a single socket.
   - **Unknown frame skipping:** sends an unassigned frame type (`0xAA`) with dummy payload and verifies the server cleanly drains it and answers subsequent requests with `200 OK`.

---

## Repository Structure

```text
binary-http/
├── src/
│   ├── observe.c           # Web server implementation
│   ├── bcurl.c             # Command-line client with verbose hexdump
│   ├── protocol.c          # Frame payload encoder & decoder
│   ├── frame.c             # Fixed 9-byte frame header serializer & parser
│   ├── headers.c           # Static compression table and header list codec
│   ├── net.c               # TCP socket helpers (read_exact, write_all, discard_exact)
│   └── util.c              # URL parser, safe path resolver, MIME detector
├── include/
│   ├── protocol.h          # Core protocol constants and structures
│   ├── frame.h             # Frame header interface
│   ├── headers.h           # Header table definitions & cross-platform types
│   ├── net.h               # Networking interface
│   └── util.h              # Utility and path resolution interface
├── tests/
│   ├── test_frames.c       # Unit tests for frame headers
│   ├── test_headers.c      # Unit tests for header compression table
│   ├── test_protocol.c     # Wire-level frame tests
│   ├── test_integration.sh # Automated end-to-end integration test runner
│   └── interop_test.py     # Independent Python socket test client
├── www/
│   ├── index.html          # Sample HTML test resource
│   ├── hello.txt           # Sample plain text test resource
│   └── sample.json         # Sample JSON test resource
├── .gitignore              # Clean git configuration
├── Makefile                # Build configuration (make all, make test, make clean)
└── README.md               # Documentation and usage guide
```

---

## Technical Highlights

- **Standard C99:** Written cleanly without proprietary compiler extensions.
- **Robust Sockets:** Fully handles TCP stream fragmentation and partial reads/writes.
- **Clean Separation of Concerns:** Network I/O, binary framing, header encoding, and filesystem routing are organized into modular files.
- **Zero Warnings:** Compiles with zero warnings under `-Wall -Wextra -Wpedantic -std=c99`.