# Binary HTTP (BHTTP)
**A Lightweight Binary Application-Layer Network Protocol in C**

---

##  Executive Summary

**Binary HTTP (BHTTP)** is an application-layer network protocol designed and implemented in standard C (C99) directly over raw TCP sockets (`SOCK_STREAM`). 

Traditional HTTP/1.1 relies on human-readable text formats, whitespace delimiter scanning (`\r\n\r\n`), and regex parsing, making it susceptible to request smuggling, delimiter injection, and parsing ambiguities. **BHTTP** replaces text framing with:
1. A **fixed 9-byte binary frame header** with deterministic length fields.
2. An **indexed static header table** combined with 16-bit length-prefixed values (inspired by HPACK).
3. Robust **TCP stream fragmentation handling** via deterministic stream assembly routines (`read_exact`, `write_all`).
4. **Persistent connections** enabling multiple sequential request/response exchanges across a single TCP socket.
5. A **strict security sandbox** preventing directory traversal attacks.

The project provides two core applications:
- **`observe`** (also aliased as **`bserve`**): A persistent BHTTP web server serving files from a document root.
- **`bcurl`**: A command-line client supporting verbose frame debugging (`-v`) with wire-level hexdumps.

---

##  Table of Contents

1. [Core Protocol Concepts](#-core-protocol-concepts)
   - [Why Binary Framing?](#1-why-binary-framing)
   - [9-Byte Frame Header Format](#2-9-byte-frame-header-format)
   - [Static Header Table & Literal Headers](#3-static-header-table--literal-headers)
   - [TCP Stream Handling](#4-tcp-stream-handling-partial-reads--writes)
   - [Persistent Connection Model](#5-persistent-connection-model)
   - [Security & Path Traversal Prevention](#6-security--path-traversal-prevention)
   - [Extensibility & Unknown Frame Handling](#7-extensibility--unknown-frame-handling)
2. [Quick Start & Build Instructions](#-quick-start--build-instructions)
   - [Building on Linux](#building-on-linux)
   - [Building & Running on Windows (via WSL)](#building--running-on-windows-via-wsl)
3. [Running the Server & Client](#-running-the-server--client)
4. [Example Outputs & Wire Traces](#-example-outputs--wire-traces)
5. [Testing Suite & Validation](#-testing-suite--validation)
6. [Repository Structure](#-repository-structure)

---

##  Core Protocol Concepts

### 1. Why Binary Framing?
- **No Delimiter Injection:** In text HTTP, messages terminate with `\r\n\r\n`. Any payload containing accidental or malicious line breaks requires complex chunked encoding. In BHTTP, payload length is explicitly defined in binary, making delimiter injection mathematically impossible.
- **Constant-Time Header Parsing:** Reading a fixed 9-byte header takes a predictable, constant number of operations without buffer scanning or backtracking.
- **Bandwidth Efficiency:** Frequent header keys (such as `content-type`, `host`, `user-agent`) compress down to a single byte rather than repetitive ASCII strings.

### 2. 9-Byte Frame Header Format
Every BHTTP message begins with an unpadded, 9-byte binary header transmitted in **Network Byte Order (Big-Endian)**:

```
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

| Field | Width | Offset | Purpose |
| :--- | :--- | :--- | :--- |
| **Length** | 24 bits (3 bytes) | `0..2` | Payload size in bytes (excluding header). Max size: $2^{24}-1$ (~16 MB). Avoids 64 KB 16-bit limits while preventing 4 GB memory exhaustion DoS attacks. |
| **Type** | 8 bits (1 byte) | `3` | Frame identifier: `0x01` (`FRAME_REQUEST`), `0x02` (`FRAME_RESPONSE`). |
| **Flags** | 8 bits (1 byte) | `4` | Modifiers: `0x01` (`FLAG_END_STREAM`) indicates the message payload is complete. |
| **Reserved (R)** | 1 bit | `5` (MSB) | Reserved bit (set to `0`). |
| **Stream ID** | 31 bits | `5..8` | Monotonically increasing transaction ID (supports over 2 billion sequential streams). |

### 3. Static Header Table & Literal Headers
Rather than repeatedly transmitting strings like `"content-type"`, BHTTP maps the 10 most common header names to 1-byte numeric IDs:

| ID (Hex) | Static Header Name | ID (Hex) | Static Header Name |
| :---: | :--- | :---: | :--- |
| `0x01` | `:method` | `0x06` | `host` |
| `0x02` | `:path` | `0x07` | `user-agent` |
| `0x03` | `:status` | `0x08` | `server` |
| `0x04` | `content-length` | `0x09` | `accept` |
| `0x05` | `content-type` | `0x0A` | `connection` |

- **Indexed Header Encoding:** `[1 byte ID][2 bytes Value Length (Big-Endian)][Value Bytes]`
- **Literal/Custom Header Encoding (`0x00`):** For headers not in the table:  
  `[0x00][2 bytes Name Length][Name Bytes][2 bytes Value Length][Value Bytes]`
- **Binary-Safe Values:** Because values are prefixed by a 16-bit length instead of null characters (`\0`), values may contain arbitrary binary bytes.

### 4. TCP Stream Handling (Partial Reads & Writes)
TCP is an **unstructured byte stream** with no record boundaries. A single `send()` can be split into multiple `recv()` chunks across network buffers.

To guarantee deterministic message assembly, BHTTP implements three core socket helpers in `src/net.c`:
- `net_read_exact(fd, buf, len)`: Loops `recv()` until exactly `len` bytes have arrived, handling `EINTR` signals and detecting clean vs. truncated disconnects.
- `net_write_all(fd, buf, len)`: Loops `send()` until all bytes have entered the socket buffer, accommodating socket backpressure.
- `net_discard_exact(fd, len)`: Drains unneeded bytes from the socket into a scratch buffer without memory leaks.

### 5. Persistent Connection Model
BHTTP connections default to persistent operation:
1. The server reads the 9-byte header and processes the request.
2. The server sends the response frame.
3. Instead of closing the socket, the server immediately loops back to read the next frame on the same socket descriptor.
4. When the client closes the connection, `recv()` returns `0`, and the server cleanly exits the connection loop.

### 6. Security & Path Traversal Prevention
Web servers must prevent directory traversal attacks (e.g., `GET /../../etc/passwd`). The server enforces sandboxing in `resolve_safe_path()`:
- **Syntactic Rejection:** Any path containing `..`, backslashes (`\`), or null bytes is immediately rejected with `400 Bad Request`.
- **Filesystem Canonicalization:** Resolves paths via `realpath()` and verifies that the canonical file path strictly starts with the canonical document root directory.
- **Status Codes:**
  - `200 OK`: File successfully located and served.
  - `400 Bad Request`: Malformed frame, path traversal attempt, or unsupported method.
  - `404 Not Found`: File does not exist within the document root.
  - `500 Internal Server Error`: Filesystem I/O or allocation error.

### 7. Extensibility & Unknown Frame Handling
If an endpoint encounters an unknown frame type (e.g. `0xAA`), it reads the 24-bit length, calls `net_discard_exact()`, and continues processing subsequent frames. This allows future protocol versions (e.g. Version 2) to introduce new frames without breaking Version 1 endpoints.

---

##  Quick Start & Build Instructions

### Prerequisites
- GCC / Clang (with C99 support)
- GNU Make
- POSIX socket environment (Linux, macOS, or Windows via WSL)

---

### Building on Linux

```bash
# 1. Compile the server, client, and test suites
make all

# 2. Run all unit and integration tests
make test

# 3. Clean all build artifacts
make clean
```

---

### Building & Running on Windows (via WSL)

Because BHTTP utilizes standard POSIX/BSD socket APIs (`<sys/socket.h>`, `<netdb.h>`), Windows users can run and test everything seamlessly using **WSL (Windows Subsystem for Linux)**.

#### Option A: Running from inside WSL Terminal
1. Open PowerShell and launch WSL:
   ```powershell
   wsl
   ```
2. Navigate to your repository directory:
   ```bash
   cd /.../binary-http
   ```
3. Build and test:
   ```bash
   make clean
   make all
   make test
   ```

#### Option B: Direct PowerShell Commands (One-Liners)
You can build, test, and run the project directly from PowerShell without entering the WSL shell interactively:

```powershell
# Compile the project
wsl make all

# Run the complete test suite
wsl make test

# Start the server on port 9000
wsl ./observe ./www 9000

# In a separate PowerShell window, run the client
wsl ./bcurl localhost:9000/index.html
```

> **Note on IDE / Editor Support:** All header files (`include/headers.h`, `include/net.h`, `include/protocol.h`) include portable fallbacks for `ssize_t` so that Windows language servers (clangd, MSVC IntelliSense) display **zero errors or missing header warnings**.

---

##  Running the Server & Client

### 1. Start the Server
Open Terminal 1:
```bash
./observe <document_root> <port>
```
Example:
```bash
./observe ./www 9000
```
*(You can also use `./bserve ./www 9000`)*

### 2. Run the Client (`bcurl`)
Open Terminal 2:

#### Request HTML document:
```bash
./bcurl localhost:9000/index.html
```

#### Request Plain Text:
```bash
./bcurl localhost:9000/hello.txt
```

#### Request JSON:
```bash
./bcurl localhost:9000/sample.json
```

#### Request with Verbose Wire Debugging (`-v`):
```bash
./bcurl -v localhost:9000/hello.txt
```

---

##  Example Outputs & Wire Traces

### Normal Execution (Clean `stdout`)
```bash
$ ./bcurl localhost:9000/hello.txt
Hello, BHTTP!
This is a plain text file served cleanly over the custom binary protocol.
```

### Verbose Mode (`-v` on `stderr` + payload on `stdout`)
```bash
$ ./bcurl -v localhost:9000/hello.txt
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

### Error Handling Demonstration

#### 1. File Not Found (404)
```bash
$ ./bcurl -v localhost:9000/does_not_exist.html
* Connecting to localhost port 9000...
* Connected successfully.
...
< Status: 404
< :status: 404
< content-type: text/plain
< server: observe/1.0
404 Not Found
```
*(Returns non-zero exit code `1`)*

#### 2. Directory Traversal Rejection (400)
```bash
$ ./bcurl -v localhost:9000/../../etc/passwd
* Connecting to localhost port 9000...
* Connected successfully.
...
< Status: 400
< :status: 400
< content-type: text/plain
< server: observe/1.0
400 Bad Request
```
*(Blocked syntactically and sandboxed)*

---

## Testing Suite & Validation

Run the automated test suite with:
```bash
make test
```

### Test Coverage Summary:
1. **Unit Tests:**
   - `tests/test_frames.c`: Validates 9-byte header packing, bitwise masking, and 24-bit length boundary conditions (`0` to `16,777,215`).
   - `tests/test_headers.c`: Validates static table lookups (`1..10`), custom literal header codec (`0x00`), and malformed length detection.
   - `tests/test_protocol.c`: Verifies byte-for-byte exact equality between serialized frames and wire expectations.
2. **Integration Tests (`tests/test_integration.sh`):**
   - Automatically spins up the server in the background.
   - Fetches and verifies exact diffs of HTML, TXT, and JSON files.
   - Tests 404 Not Found and 400 Bad Request handling.
   - Tests `stdout`/`stderr` channel separation in verbose mode.
3. **Independent Interoperability Harness (`tests/interop_test.py`):**
   - An independent Python socket client building raw binary frames directly.
   - Validates persistent transactions across a single TCP socket.
   - Tests the **unknown-frame extensibility rule**: sends unknown frame `0xAA` with 20 bytes of dummy payload, verifies the server cleanly discards it and answers subsequent requests with `200 OK`.

---

##  Repository Structure

```
binary-http/
├── src/
│   ├── observe.c         # BHTTP server implementation
│   ├── bcurl.c           # Command-line client with verbose hexdump
│   ├── protocol.c        # Request/Response payload encoder & decoder
│   ├── frame.c           # Fixed 9-byte frame header serializer & parser
│   ├── headers.c         # Static table and header list codec
│   ├── net.c             # Socket stream helpers (read_exact, write_all, discard_exact)
│   └── util.c            # URL parser, safe path resolver, MIME detector, hexdump printer
├── include/
│   ├── protocol.h        # Protocol constants and data structures
│   ├── frame.h           # Frame header definitions
│   ├── headers.h         # Header table definitions & cross-platform ssize_t
│   ├── net.h             # Network socket interfaces
│   └── util.h            # Path resolution and utility interfaces
├── tests/
│   ├── test_frames.c     # Unit tests for frame header encoding
│   ├── test_headers.c    # Unit tests for header compression table
│   ├── test_protocol.c   # Wire-level reference validation
│   ├── test_integration.sh # Automated end-to-end integration test runner
│   └── interop_test.py   # Independent Python socket test client
├── www/
│   ├── index.html        # Sample HTML test resource
│   ├── hello.txt         # Sample plain text test resource
│   └── sample.json       # Sample JSON test resource
├── .gitignore            # Clean git configuration for assignment submission
├── Makefile              # Build configuration with all, test, clean targets
└── README.md             # Complete user guide and technical documentation
```

---

##  Summary of Technical Highlights

- **Standard C99:** Written cleanly without proprietary compiler extensions.
- **Robust POSIX Sockets:** Comprehensive handling of partial transfers and interrupted system calls.
- **Clean Architecture:** Strict separation between framing, header compression, network transport, and filesystem routing.
- **Zero Compiler Warnings:** Compiles cleanly with `-Wall -Wextra -Wpedantic -std=c99`.