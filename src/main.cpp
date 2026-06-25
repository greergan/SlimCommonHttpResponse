#define HWY_TARGET_INCLUDE "slim/common/http/response-inl.h"
#include "hwy/foreach_target.h"
#undef HWY_TARGET_INCLUDE
#include "slim/common/http/response-inl.h"

#include <charconv>
#include <format>
#include <algorithm>
#include <cctype>
#include <vector>
#include <slim/common/http/headers.h>
#include <slim/common/http/response.h>
#include <slim/common/utilities.h>

namespace slim::common::http {

HWY_EXPORT(parse_headers);

namespace {
    struct StatusLine {
        int              code;
        std::string_view version;
        std::string_view text;
    };

    bool iequals(std::string_view a, std::string_view b) {
        return std::ranges::equal(a, b, [](char c1, char c2) {
            return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
        });
    }

    std::string_view trim_right(std::string_view s) {
        size_t end = s.find_last_not_of(" \t");
        return (end == std::string_view::npos) ? "" : s.substr(0, end + 1);
    }

    std::string_view trim(std::string_view s) {
        size_t start = s.find_first_not_of(" \t");
        if (start == std::string_view::npos) return "";
        size_t end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    }

    // Returns true if transfer-encoding header indicates chunked transfer.
    // Per RFC 7230 §3.3.1, "chunked" must be the final encoding in the list.
    bool is_chunked_transfer(std::string_view header_value) {
        std::string_view last_token;
        size_t pos = 0;
        while (pos < header_value.size()) {
            size_t comma = header_value.find(',', pos);
            std::string_view token = (comma == std::string_view::npos)
                ? header_value.substr(pos)
                : header_value.substr(pos, comma - pos);
            token = trim(token);
            if (!token.empty())
                last_token = token;
            if (comma == std::string_view::npos) break;
            pos = comma + 1;
        }
        return iequals(last_token, "chunked");
    }

    slim::ErrorInfo decode_chunked_body(std::span<const uint8_t> storage, size_t cursor, slim::slim_storage_container& out_body) {
        while (cursor < storage.size()) {
            std::string_view rem(reinterpret_cast<const char*>(storage.data() + cursor), storage.size() - cursor);
            size_t line_end = rem.find("\r\n");
            if (line_end == std::string_view::npos) return slim::ErrorInfo("incomplete chunked encoding: missing CRLF");

            // Strip chunk extensions (e.g. "a; ext=val") before parsing the hex size
            std::string_view chunk_line = rem.substr(0, line_end);
            size_t ext = chunk_line.find(';');
            if (ext != std::string_view::npos)
                chunk_line = chunk_line.substr(0, ext);
            chunk_line = trim(chunk_line);

            size_t chunk_size = 0;
            auto [ptr, ec] = std::from_chars(chunk_line.data(), chunk_line.data() + chunk_line.size(), chunk_size, 16);
            if (ec != std::errc{}) return slim::ErrorInfo(std::format("invalid chunk size hex: {}", chunk_line));
            if (chunk_size == 0) break;

            cursor += line_end + 2;
            if (cursor + chunk_size > storage.size()) return slim::ErrorInfo("chunk size exceeds remaining payload length");

            out_body.insert(out_body.end(), storage.data() + cursor, storage.data() + cursor + chunk_size);
            cursor += chunk_size;

            if (cursor + 2 > storage.size() || storage[cursor] != '\r' || storage[cursor + 1] != '\n')
                return slim::ErrorInfo("missing CRLF after chunk data");
            cursor += 2;
        }

        return {};
    }

    slim::ErrorInfo parse_status_line(std::string_view raw, StatusLine& out, size_t& headers_start) {
        size_t line_end = raw.find("\r\n");
        if (line_end == std::string_view::npos) return slim::ErrorInfo("response is unparsable");

        std::string_view line = raw.substr(0, line_end);
        size_t first_space = line.find(' ');
        if (first_space == std::string_view::npos) return slim::ErrorInfo("response is unparsable");

        std::string_view after_version = line.substr(first_space + 1);
        size_t second_space = after_version.find(' ');

        std::string_view code_sv;
        if (second_space == std::string_view::npos) {
            code_sv  = after_version;
            out.text = "";
        } else {
            code_sv  = after_version.substr(0, second_space);
            out.text = trim_right(after_version.substr(second_space + 1));
        }

        int code = 0;
        auto [ptr, ec] = std::from_chars(code_sv.data(), code_sv.data() + code_sv.size(), code);
        if (ec != std::errc{}) return slim::ErrorInfo(static_cast<int>(ec), std::format("failed to parse status code '{}'", code_sv));
        if (code < 100 || code > 599) return slim::ErrorInfo(std::format("response code out of range => {} should be > 99 and < 600", code));

        out.version      = line.substr(0, first_space);
        out.code         = code;
        headers_start    = line_end + 2;
        return {};
    }

    void parse(Response& r, std::span<const uint8_t> storage) {
        if (storage.empty()) {
            r.error_info = slim::ErrorInfo(std::format("{} => response storage is empty", __func__));
            return;
        }

        std::string_view raw(reinterpret_cast<const char*>(storage.data()), storage.size());
        StatusLine status;
        size_t headers_start;
        r.error_info = parse_status_line(raw, status, headers_start);
        if (r.error_info.has_error()) return;

        size_t body_start = storage.size();
        r.error_info = HWY_DYNAMIC_DISPATCH(parse_headers)(raw, headers_start, r.headers, body_start);
        if (r.error_info.has_error()) return;

        r.code         = status.code;
        r.code_text    = std::string(status.text);
        r.http_version = std::string(status.version);

        auto te = r.headers.get("transfer-encoding");
        if (te && is_chunked_transfer(te.to_string())) {
            if (body_start < storage.size())
                r.error_info = decode_chunked_body(storage, body_start, r.body);
        } else {
            // Validate Content-Length when present — must happen regardless of
            // whether any body bytes arrived, so truncated responses are caught.
            auto cl_value = r.headers.get("content-length");
            if (cl_value) {
                std::string_view cl = cl_value.to_string();
                size_t declared = 0;
                auto [ptr, ec] = std::from_chars(cl.data(), cl.data() + cl.size(), declared);
                if (ec != std::errc{}) {
                    r.error_info = slim::ErrorInfo(std::format("invalid Content-Length value: {}", cl));
                    return;
                }

                size_t available = storage.size() - body_start;
                if (available < declared) {
                    r.error_info = slim::ErrorInfo(std::format(
                        "Content-Length {} exceeds available body bytes {}", declared, available));
                    return;
                }

                if (declared > 0)
                    r.body = slim::slim_storage_container(
                        storage.data() + body_start,
                        storage.data() + body_start + declared);
            } else if (body_start < storage.size()) {
                auto body_span = storage.subspan(body_start);
                r.body = slim::slim_storage_container(body_span.begin(), body_span.end());
            }
        }
    }
} // namesapce

    Response::Response() {}
    Response::Response(std::span<const uint8_t> storage) { parse(*this, storage); }
    bool Response::has_error() const { return error_info.has_error(); }

} // namespace slim::common::http
