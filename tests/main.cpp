#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <slim/common/http/headers.h>
#include <slim/common/http/response.h>

using namespace slim::common::http;

namespace {
    std::span<const uint8_t> to_span(const std::string& s) {
        return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
    }
}

// ---------------------------------------------------------------------------
// Status line
// ---------------------------------------------------------------------------

TEST_CASE("Empty input", "[response]") {
    Response response(to_span(""));
    REQUIRE(response.has_error());
}

TEST_CASE("Missing status line CRLF", "[response]") {
    Response response(to_span("Invalid data with no CRLF"));
    REQUIRE(response.has_error());
}

TEST_CASE("Non-numeric response code", "[response]") {
    Response response(to_span("HTTP/1.1 ABC OK\r\n\r\n"));
    REQUIRE(response.has_error());
}

TEST_CASE("Response code out of range", "[response]") {
    Response response(to_span("HTTP/1.1 999 Whatever\r\n\r\n"));
    REQUIRE(response.has_error());
}

TEST_CASE("Status line with no reason phrase", "[response]") {
    // HTTP/1.1 allows omitting the reason phrase entirely
    Response response(to_span("HTTP/1.1 204\r\n\r\n"));
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.code == 204);
    REQUIRE(response.code_text.empty());
}

TEST_CASE("1xx response code is valid", "[response]") {
    Response response(to_span("HTTP/1.1 100 Continue\r\n\r\n"));
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.code == 100);
}

TEST_CASE("3xx response code is valid", "[response]") {
    Response response(to_span("HTTP/1.1 301 Moved Permanently\r\nLocation: https://example.com/\r\n\r\n"));
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.code == 301);
    REQUIRE(response.headers.get("Location") == "https://example.com/");
}

TEST_CASE("4xx response code is valid", "[response]") {
    Response response(to_span("HTTP/1.1 404 Not Found\r\n\r\n"));
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.code == 404);
}

TEST_CASE("5xx response code is valid", "[response]") {
    Response response(to_span("HTTP/1.1 500 Internal Server Error\r\n\r\n"));
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.code == 500);
}

// ---------------------------------------------------------------------------
// Headers
// ---------------------------------------------------------------------------

TEST_CASE("OK response", "[response]") {
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.http_version == "HTTP/1.1");
    REQUIRE(response.code == 200);
    REQUIRE(response.code_text == "OK");
    REQUIRE(response.headers.entries().size() == 1);
    REQUIRE(response.headers.get("Content-Type") == "text/plain");
}

TEST_CASE("Response with multiple headers", "[response]") {
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.headers.entries().size() == 3);
    REQUIRE(response.headers.get("Content-Type") == "text/html");
    REQUIRE(response.headers.get("Cache-Control") == "no-cache");
    REQUIRE(response.headers.get("Connection") == "keep-alive");
}

TEST_CASE("Malformed header line is skipped", "[response]") {
    std::string raw = "HTTP/1.1 200 OK\r\nInvalidHeader\r\nContent-Type: text/plain\r\n\r\n";

    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.headers.entries().size() == 1);
    REQUIRE(response.headers.get("Content-Type") == "text/plain");
}

TEST_CASE("Header value whitespace is trimmed", "[response]") {
    std::string raw = "HTTP/1.1 200 OK\r\nX-Custom-Header:    value with spaces    \r\n\r\n";

    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.headers.get("X-Custom-Header") == "value with spaces");
}

TEST_CASE("Truncated input mid-headers", "[response]") {
    // Header section never terminated with \r\n\r\n
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
    Response response(to_span(raw));
    REQUIRE(response.has_error());
}

// ---------------------------------------------------------------------------
// Body — identity
// ---------------------------------------------------------------------------

TEST_CASE("Response with body", "[response]") {
    std::string body = "Hello, World!";
    std::string raw  = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n" + body;

    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.code == 200);
    std::string body_str(response.body.begin(), response.body.end());
    REQUIRE(body_str == body);
}

TEST_CASE("Content-Length larger than available bytes is an error", "[response]") {
    // Declares 50 bytes but body is only 13
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nHello, World!";
    Response response(to_span(raw));
    REQUIRE(response.has_error());
}

TEST_CASE("Content-Length smaller than body trims to declared length", "[response]") {
    // Server sends more bytes than Content-Length; we should respect the header
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello, World!";
    Response response(to_span(raw));
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    std::string body_str(response.body.begin(), response.body.end());
    REQUIRE(body_str == "Hello");
}

TEST_CASE("Invalid Content-Length value is an error", "[response]") {
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nHello";
    Response response(to_span(raw));
    REQUIRE(response.has_error());
}

// ---------------------------------------------------------------------------
// Body — chunked
// ---------------------------------------------------------------------------

TEST_CASE("Transfer-Encoding chunked", "[response]") {
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Date: Sun, 31 May 2026 21:16:56 GMT\r\n"
        "Content-Type: text/html\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "Server: cloudflare\r\n"
        "Last-Modified: Thu, 28 May 2026 04:54:11 GMT\r\n"
        "Allow: GET, HEAD\r\n"
        "Accept-Ranges: bytes\r\n"
        "\r\n"
        "d\r\n"
        "<html></html>\r\n"
        "d\r\n"
        "<html></html>\r\n"
        "0\r\n"
        "\r\n";

    std::vector<uint8_t> body = {60,104,116,109,108,62,60,47,104,116,109,108,62,
                                  60,104,116,109,108,62,60,47,104,116,109,108,62};
    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.body == body);
}

TEST_CASE("Transfer-Encoding chunked complex example", "[response]") {
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Date: Sun, 31 May 2026 21:16:56 GMT\r\n"
        "Content-Type: text/html\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: keep-alive\r\n"
        "Server: cloudflare\r\n"
        "Last-Modified: Thu, 28 May 2026 04:54:11 GMT\r\n"
        "Allow: GET, HEAD\r\n"
        "Accept-Ranges: bytes\r\n"
        "Age: 13533\r\n"
        "cf-cache-status: HIT\r\n"
        "CF-RAY: a04900a469a40ad7-LAS\r\n"
        "\r\n"
        "20f\r\n"   // 0x20f = 527
        "<!doctype html><html lang='en'><head><title>Example Domain</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#eee;width:60vw;margin:15vh auto;font-family:system-ui,sans-serif}h1{font-size:1.5em}div{opacity:0.8}a:link,a:visited{color:#348}</style></head><body><div><h1>Example Domain</h1><p>This domain is for use in documentation examples without needing permission. Avoid use in operations.</p><p><a href='https://iana.org/domains/example'>Learn more</a></p></div></body></html>\r\n"
        "0\r\n"
        "\r\n";

    std::string html_body =
        "<!doctype html><html lang='en'><head><title>Example Domain</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#eee;width:60vw;margin:15vh auto;font-family:system-ui,sans-serif}h1{font-size:1.5em}div{opacity:0.8}a:link,a:visited{color:#348}</style></head><body><div><h1>Example Domain</h1><p>This domain is for use in documentation examples without needing permission. Avoid use in operations.</p><p><a href='https://iana.org/domains/example'>Learn more</a></p></div></body></html>";

    std::vector<uint8_t> body(html_body.begin(), html_body.end());

    Response response(to_span(raw));
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());

    REQUIRE_FALSE(response.has_error());
    REQUIRE(response.body == body);
}

TEST_CASE("Transfer-Encoding chunked with chunk extensions are ignored", "[response]") {
    // RFC 7230 §4.1.1: chunk-ext is defined but recipients must ignore unknown extensions
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "d; name=metadata; foo=bar\r\n"
        "<html></html>\r\n"
        "0\r\n"
        "\r\n";

    std::string expected = "<html></html>";
    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    std::string body_str(response.body.begin(), response.body.end());
    REQUIRE(body_str == expected);
}

TEST_CASE("Transfer-Encoding list with chunked as final encoding", "[response]") {
    // RFC 7230 §3.3.1: chunked must be the last transfer-encoding
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: gzip, chunked\r\n"
        "\r\n"
        "d\r\n"
        "<html></html>\r\n"
        "0\r\n"
        "\r\n";

    Response response(to_span(raw));

    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    // Body bytes are the raw (still gzip-compressed) chunk payload —
    // decompression is out of scope, but chunked framing must be decoded.
    std::string body_str(response.body.begin(), response.body.end());
    REQUIRE(body_str == "<html></html>");
}

TEST_CASE("Transfer-Encoding with chunked not last is not decoded as chunked", "[response]") {
    // "chunked, gzip" — chunked is NOT last, so must not be treated as chunked framing
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked, gzip\r\n"
        "\r\n"
        "some raw body bytes\r\n";

    Response response(to_span(raw));

    // No chunked decoding; raw body bytes passed through as-is
    if (response.has_error())
        UNSCOPED_INFO(response.error_info.message());
    REQUIRE_FALSE(response.has_error());
    std::string body_str(response.body.begin(), response.body.end());
    REQUIRE(body_str == "some raw body bytes\r\n");
}

TEST_CASE("Truncated chunk body is an error", "[response]") {
    // Declares 0xff bytes but only provides 5
    std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "ff\r\n"
        "short\r\n"
        "0\r\n"
        "\r\n";

    Response response(to_span(raw));
    REQUIRE(response.has_error());
}