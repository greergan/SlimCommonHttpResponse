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

TEST_CASE("Empty input", "[response]") {
    Response response(to_span(""));
    REQUIRE(response.has_error());
}

TEST_CASE("Missing status line", "[response]") {
    Response response(to_span("Invalid data with no CRLF"));
    REQUIRE(response.has_error());
}

TEST_CASE("OK response", "[response]") {
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n";
    Response response(to_span(raw));

    REQUIRE(!response.has_error());
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

    REQUIRE(!response.has_error());
    REQUIRE(response.headers.entries().size() == 3);
    REQUIRE(response.headers.get("Content-Type") == "text/html");
    REQUIRE(response.headers.get("Cache-Control") == "no-cache");
    REQUIRE(response.headers.get("Connection") == "keep-alive");
}

TEST_CASE("Response with body", "[response]") {
    std::string body = "Hello, World!";
    std::string raw  = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n" + body;

    Response response(to_span(raw));

    REQUIRE(!response.has_error());
    REQUIRE(response.code == 200);
    std::string body_str(response.body.begin(), response.body.end());
    REQUIRE(body_str == body);
}

TEST_CASE("Malformed header line is skipped", "[response]") {
    std::string raw = "HTTP/1.1 200 OK\r\nInvalidHeader\r\nContent-Type: text/plain\r\n\r\n";

    Response response(to_span(raw));

    REQUIRE(!response.has_error());
    REQUIRE(response.headers.entries().size() == 1);
    REQUIRE(response.headers.get("Content-Type") == "text/plain");
}

TEST_CASE("Header value whitespace is trimmed", "[response]") {
    std::string raw = "HTTP/1.1 200 OK\r\nX-Custom-Header:    value with spaces    \r\n\r\n";

    Response response(to_span(raw));

    REQUIRE(!response.has_error());
    REQUIRE(response.headers.get("X-Custom-Header") == "value with spaces");
}

TEST_CASE("Response code out of range", "[response]") {
    std::string raw = "HTTP/1.1 999 Whatever\r\n\r\n";

    Response response(to_span(raw));

    REQUIRE(response.has_error());
}

TEST_CASE("Non-numeric response code", "[response]") {
    std::string raw = "HTTP/1.1 ABC OK\r\n\r\n";

    Response response(to_span(raw));

    REQUIRE(response.has_error());
}