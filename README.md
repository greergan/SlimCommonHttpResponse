<a href="https://codeberg.org/greergan/SlimTS">
  <img src="https://raw.githubusercontent.com/greergan/SlimTS/master/assets/slimts_logo.png" width="75" alt="SlimTS Logo">
</a>

# SlimCommonHttpResponse

A lightweight, RFC-oriented HTTP response parser in modern C++.  
Part of the [SlimCommon](https://codeberg.org/greergan/SlimCommon) library.  
Built using [SlimLibraryPackager](https://codeberg.org/greergan/SlimLibraryPackager).  
CI/CD supplied by unified workflows provided by [SlimLibraryPackager](https://codeberg.org/greergan/SlimLibraryPackager).

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Core API](#core-api)
  - [ErrorStatus enum](#errorstatus-enum)
  - [ResponseParseException](#responseparseexception)
  - [Response struct](#response-struct)
  - [Constructors and object lifetime](#constructors-and-object-lifetime)
  - [Members](#members)
- [Building](#building)
- [Dependencies](#dependencies)
  - [required_packages](#required_packages)
  - [external_dependencies](#external_dependencies)
  - [slim_flags](#slim_flags)
- [Examples](#examples)

## Overview

This library provides a strict HTTP response parser with Highway-vectorized header parsing, chunked transfer-encoding decoding, and explicit error reporting. It is designed for use in environments where predictable behaviour, minimal overhead, and strong validation are required.

Header parsing is accelerated using [Google Highway](https://github.com/google/highway) via a per-target dispatch model. The vectorized path scans for `:`, `\r`, `\n`, and whitespace in parallel, falling back to scalar processing only at boundaries or special-case bytes.

[↑ Top](#table-of-contents)

## Features

| Feature | Description |
|---------|-------------|
| Status line parsing | Extracts HTTP version, numeric status code (100–599), and reason phrase |
| Vectorized header parsing | Highway SIMD dispatch across all available targets via `response-inl.h` |
| Chunked decoding | Full chunked transfer-encoding body assembly with chunk-extension stripping |
| Content-Length body | Exact-length body copy with mismatch detection |
| Header access | Via `SlimCommonHttpHeaders` — case-insensitive get, multi-value support |
| Error model | Strong enum-based status reporting via `ErrorStatus` (from [SlimCommonHttp](https://codeberg.org/greergan/SlimCommonHttp)) |
| Exception on parse failure | Constructor throws `ResponseParseException` on any parse error |

[↑ Top](#table-of-contents)

## Core API

### ErrorStatus enum

`ErrorStatus` is the scoped enum provided by [SlimCommonHttp](https://codeberg.org/greergan/SlimCommonHttp).

Values are grouped by concern:

| Group | Values | Meaning |
|-------|--------|---------|
| Storage | `ResponseStorageEmpty` | Input buffer was empty |
| Status line | `ResponseStatusLineInvalid`, `ResponseStatusCodeInvalid`, `ResponseStatusCodeOutOfRange` | Status line could not be parsed or code was outside 100–599 |
| Headers | `ResponseHeadersBareCR`, `ResponseHeadersTerminatorMalformed`, `ResponseHeadersNotTerminated`, `HeaderValueInvalidFolding` | Header block is malformed or unterminated |
| Chunked body | `ResponseChunkedMissingCRLF`, `ResponseChunkedSizeInvalid`, `ResponseChunkedTruncated`, `ResponseChunkedMissingCRLFAfterData` | Chunked body is malformed or truncated |
| Content-Length body | `ResponseContentLengthInvalid`, `ResponseContentLengthMismatch` | Content-Length value could not be parsed or buffer is too short |
| `OK` | — | No error; the operation succeeded |

[↑ Top](#table-of-contents)

### ResponseParseException

`ResponseParseException` is thrown by the parsing constructor when any parse step fails. It carries the `ErrorStatus` value that caused the failure and is provided by [SlimCommonHttp](https://codeberg.org/greergan/SlimCommonHttp).

[↑ Top](#table-of-contents)

### Response struct

```cpp
slim::common::http::Response r(buf);
```

[↑ Top](#table-of-contents)

### Constructors and object lifetime

| Form | Description |
|------|-------------|
| `Response()` | Default constructor; produces an empty, unpopulated response |
| `Response(std::span<uint8_t> buf)` | Parse constructor; fully parses status line, headers, and body from `buf`. Throws `ResponseParseException` on failure |

[↑ Top](#table-of-contents)

### Members

| Member | Type | Description |
|--------|------|-------------|
| `headers` | `Headers` | Parsed headers collection |
| `version` | `std::string` | HTTP version string (e.g. `"HTTP/1.1"`) |
| `code_text` | `std::string` | Reason phrase (e.g. `"OK"`, `"Not Found"`) |
| `code` | `int` | HTTP status code (e.g. `200`, `404`), default `0` |
| `body` | `std::vector<uint8_t>` | Decoded body bytes |

[↑ Top](#table-of-contents)

## Building

This library is built using [SlimLibraryPackager](https://codeberg.org/greergan/SlimLibraryPackager). See that repository for build instructions.

[↑ Top](#table-of-contents)

## Dependencies

### required_packages

External package dependencies for this library are declared in the [`required_packages`](required_packages) file at the repository root. This file is read by [SlimLibraryPackager](https://codeberg.org/greergan/SlimLibraryPackager) during the build process to resolve dependencies and install them if not present.

```
SlimCommonUtilities 0.15.0
SlimCommonHttp 0.4.0
SlimCommonHttpHeader 0.11.0
SlimCommonHttpHeaders 0.9.2
```

- [SlimCommonUtilities](https://codeberg.org/greergan/SlimCommonUtilities) — provides `iequals` and `trim`
- [SlimCommonHttp](https://codeberg.org/greergan/SlimCommonHttp) — provides `ErrorStatus` and `ResponseParseException`
- [SlimCommonHttpHeader](https://codeberg.org/greergan/SlimCommonHttpHeader) — single header representation
- [SlimCommonHttpHeaders](https://codeberg.org/greergan/SlimCommonHttpHeaders) — header collection with case-insensitive lookup

[↑ Top](#table-of-contents)

### external_dependencies

External (non-SlimCommon) dependencies are declared in the [`external_dependencies`](external_dependencies) file at the repository root. This file is read by [SlimLibraryPackager](https://codeberg.org/greergan/SlimLibraryPackager) during the build process to resolve and install them if not present.

```
google-highway 1.4.0
```

- [Google Highway](https://github.com/google/highway) — portable SIMD used for vectorized header parsing

[↑ Top](#table-of-contents)

### slim_flags

Compiler and linker flags are declared in the [`slim_flags`](slim_flags) file at the repository root. This file is read by [SlimLibraryPackager](https://codeberg.org/greergan/SlimLibraryPackager) during the build process to apply the necessary flags.

```
LD_FLAGS -lhwy
```

[↑ Top](#table-of-contents)

## Examples

```cpp
// Parse a response buffer
try {
    std::vector<uint8_t> buf = /* received from network */;
    slim::common::http::Response r(std::span<uint8_t>(buf));

    std::cout << r.code << ' ' << r.code_text << '\n'; // -> "200 OK"
    std::cout << r.version << '\n';                     // -> "HTTP/1.1"
}
catch (const slim::common::http::ResponseParseException& e) {
    std::cerr << "Parse failed: " << e.what() << '\n';
}
```

```cpp
// Access response headers
try {
    slim::common::http::Response r(std::span<uint8_t>(buf));

    if (auto ct = r.headers.get("content-type"); ct) {
        std::cout << ct->get_value()[0] << '\n'; // -> "application/json"
    }
}
catch (const slim::common::http::ResponseParseException& e) {
    std::cerr << "Parse failed: " << e.what() << '\n';
}
```

```cpp
// Access the decoded body
try {
    slim::common::http::Response r(std::span<uint8_t>(buf));

    const auto& body = r.body;
    std::string_view body_text(reinterpret_cast<const char*>(body.data()), body.size());
    std::cout << body_text << '\n';
}
catch (const slim::common::http::ResponseParseException& e) {
    std::cerr << "Parse failed: " << e.what() << '\n';
}
```

```cpp
// Use with SlimCommonNetworkClientTcp
try {
    slim::common::network::client::tcp::Connection conn("example.com", 443, true);

    auto status = conn.write("GET / HTTP/1.1\r\nHost: example.com\r\n\r\n");
    if (status != ErrorStatus::OK) return status;

    std::vector<uint8_t> buf;
    status = conn.read(buf);
    if (status != ErrorStatus::OK) return status;

    slim::common::http::Response r(std::span<uint8_t>(buf));
    std::cout << r.code << ' ' << r.code_text << '\n';
}
catch (const slim::common::network::NetworkException& e) {
    std::cerr << "Connection failed: " << e.what() << '\n';
}
catch (const slim::common::http::ResponseParseException& e) {
    std::cerr << "Parse failed: " << e.what() << '\n';
}
```

```cpp
// Build a response programmatically (no parsing)
slim::common::http::Response r;
r.version   = "HTTP/1.1";
r.code      = 200;
r.code_text = "OK";
```

[↑ Top](#table-of-contents)
