#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <slim/common/http/headers.h>
#include <slim/common/http/response.h>

using namespace slim::common::http;

// Helper to convert std::string to std::span<uint8_t> matching constructor signature
inline std::span<uint8_t> to_span(std::string& str) {
    return {reinterpret_cast<uint8_t*>(str.data()), str.size()};
}

// ---------------------------------------------------------------------------
// Status line
// ---------------------------------------------------------------------------

TEST_CASE("Status line parsing errors", "[response]") {
    SECTION("Empty input") {
        std::string raw = "";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }

    SECTION("Missing status line CRLF") {
        std::string raw = "Invalid data with no CRLF";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }

    SECTION("Non-numeric response code") {
        std::string raw = "HTTP/1.1 ABC OK\r\n\r\n";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }

    SECTION("Response code out of range") {
        std::string raw = "HTTP/1.1 999 Whatever\r\n\r\n";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }

    SECTION("Truncated input mid-headers") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }
}

TEST_CASE("Status line parses valid response codes", "[response]") {
    SECTION("No reason phrase") {
        std::string raw = "HTTP/1.1 204\r\n\r\n";
        Response response(to_span(raw));
        REQUIRE(response.code() == 204);
        REQUIRE(response.code_text().empty());
    }

    SECTION("1xx response code is valid") {
        std::string raw = "HTTP/1.1 100 Continue\r\n\r\n";
        Response response(to_span(raw));
        REQUIRE(response.code() == 100);
    }

    SECTION("3xx response code is valid") {
        std::string raw = "HTTP/1.1 301 Moved Permanently\r\nLocation: https://example.com/\r\n\r\n";
        Response response(to_span(raw));
        REQUIRE(response.code() == 301);
    }

    SECTION("4xx response code is valid") {
        std::string raw = "HTTP/1.1 404 Not Found\r\n\r\n";
        Response response(to_span(raw));
        REQUIRE(response.code() == 404);
    }

    SECTION("5xx response code is valid") {
        std::string raw = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        Response response(to_span(raw));
        REQUIRE(response.code() == 500);
    }

    SECTION("OK response sets version and code text") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
        Response response(to_span(raw));
        REQUIRE(response.version() == "HTTP/1.1");
        REQUIRE(response.code() == 200);
        REQUIRE(response.code_text() == "OK");
    }
}

// ---------------------------------------------------------------------------
// Body — identity
// ---------------------------------------------------------------------------

TEST_CASE("Identity body with Content-Length", "[response]") {
    SECTION("Response with body") {
        std::string body = "Hello, World!";
        std::string raw  = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n" + body;

        Response response(to_span(raw));
        REQUIRE(response.code() == 200);
        std::string body_str(response.body().begin(), response.body().end());
        REQUIRE(body_str == body);
    }

    SECTION("Content-Length larger than available bytes is an error") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\nHello, World!";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }

    SECTION("Content-Length smaller than body trims to declared length") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello, World!";
        Response response(to_span(raw));
        std::string body_str(response.body().begin(), response.body().end());
        REQUIRE(body_str == "Hello");
    }

    SECTION("Invalid Content-Length value is an error") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: abc\r\n\r\nHello";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }

    SECTION("Non-zero Content-Length with no body bytes is an error") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n";
        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }

    SECTION("Content-Length zero with no body bytes is valid") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        Response response(to_span(raw));
        REQUIRE(response.body().empty());
    }

    SECTION("Content-Length zero with body bytes produces empty body") {
        std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\nignored";
        Response response(to_span(raw));
        REQUIRE(response.body().empty());
    }
}

// ---------------------------------------------------------------------------
// Body — chunked
// ---------------------------------------------------------------------------

TEST_CASE("Chunked transfer encoding", "[response]") {
    SECTION("Transfer-Encoding chunked") {
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

        std::vector<uint8_t> expected_body = {60,104,116,109,108,62,60,47,104,116,109,108,62,
                                              60,104,116,109,108,62,60,47,104,116,109,108,62};
        Response response(to_span(raw));
        REQUIRE(response.body() == expected_body);
    }

    SECTION("Transfer-Encoding chunked complex example") {
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
            "20f\r\n"
            "<!doctype html><html lang='en'><head><title>Example Domain</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#eee;width:60vw;margin:15vh auto;font-family:system-ui,sans-serif}h1{font-size:1.5em}div{opacity:0.8}a:link,a:visited{color:#348}</style></head><body><div><h1>Example Domain</h1><p>This domain is for use in documentation examples without needing permission. Avoid use in operations.</p><p><a href='https://iana.org/domains/example'>Learn more</a></p></div></body></html>\r\n"
            "0\r\n"
            "\r\n";

        std::string html_body =
            "<!doctype html><html lang='en'><head><title>Example Domain</title><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#eee;width:60vw;margin:15vh auto;font-family:system-ui,sans-serif}h1{font-size:1.5em}div{opacity:0.8}a:link,a:visited{color:#348}</style></head><body><div><h1>Example Domain</h1><p>This domain is for use in documentation examples without needing permission. Avoid use in operations.</p><p><a href='https://iana.org/domains/example'>Learn more</a></p></div></body></html>";

        std::vector<uint8_t> expected_body(html_body.begin(), html_body.end());

        Response response(to_span(raw));
        REQUIRE(response.body() == expected_body);
    }

    SECTION("Chunk extensions are ignored") {
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
        std::string body_str(response.body().begin(), response.body().end());
        REQUIRE(body_str == expected);
    }

    SECTION("Transfer-Encoding list with chunked as final encoding") {
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

            // Output the value of each parsed token for debugging
            if (auto te_h = response.headers().get("transfer-encoding"); te_h) {
                UNSCOPED_INFO("--- Transfer-Encoding Tokens ---");
                for (const auto& val : te_h->get_value()) {
                    UNSCOPED_INFO("[" << val << "]");
                }
            } else {
                UNSCOPED_INFO("Transfer-Encoding header not found!");
            }

            // Body bytes are the raw (still gzip-compressed) chunk payload —
            // decompression is out of scope, but chunked framing must be decoded.
            std::string body_str(response.body().begin(), response.body().end());
            REQUIRE(body_str == "<html></html>");
        }

    SECTION("Transfer-Encoding with chunked not last is not decoded as chunked") {
        std::string raw =
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked, gzip\r\n"
            "\r\n"
            "some raw body bytes\r\n";

        Response response(to_span(raw));
        std::string body_str(response.body().begin(), response.body().end());
        REQUIRE(body_str == "some raw body bytes\r\n");
    }

    SECTION("Truncated chunk body is an error") {
        std::string raw =
            "HTTP/1.1 200 OK\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "ff\r\n"
            "short\r\n"
            "0\r\n"
            "\r\n";

        REQUIRE_THROWS_AS(Response(to_span(raw)), ResponseParseException);
    }
}
