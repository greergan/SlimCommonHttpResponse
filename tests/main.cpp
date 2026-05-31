#include <catch2/catch_test_macros.hpp>
#include <slim/common/http/headers.h>
#include <slim/common/http/response.h>
#include <slim/common/http/url.h>

using namespace slim::common::http;

TEST_CASE("string to Response", "[response]") {
	//Response response{"http://example.com"};
/* 	auto& headers = response.headers();
	REQUIRE(headers.entries().size() == 2);
	REQUIRE(request.method() == "GET");
	REQUIRE(request.version() == "HTTP/1.1");
	REQUIRE(headers.get("Host") == "example.com");
	REQUIRE(headers.get("Origin") == "http://example.com");
	auto request_string = request.to_string();
	REQUIRE(request_string.starts_with("GET / HTTP/1.1"));
	REQUIRE(request_string.contains("Content-Type: text/plain; charset=utf-8"));
	REQUIRE(request_string.ends_with("\r\n\r\n\r\n")); */
}
