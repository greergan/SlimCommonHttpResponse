#define HWY_TARGET_INCLUDE "slim/common/http/response-inl.h"
#include "hwy/foreach_target.h"
#undef HWY_TARGET_INCLUDE
#include "slim/common/http/response-inl.h"

#include <charconv>
#include <format>
#include <slim/common/http/response.h>

namespace slim::common::http {

HWY_EXPORT(parse_headers);

namespace {
    struct StatusLine {
        int              code;
        std::string_view version;
        std::string_view text;
    };

    slim::ErrorInfo parse_status_line(std::string_view raw, StatusLine& out, size_t& headers_start) {
        size_t line_end = raw.find("\r\n");
        if (line_end == std::string_view::npos)
            return slim::ErrorInfo(std::format("{} => response is unparsable", __func__));

        std::string_view line = raw.substr(0, line_end);

        size_t first_space = line.find(' ');
        if (first_space == std::string_view::npos)
            return slim::ErrorInfo(std::format("{} => response is unparsable", __func__));

        std::string_view after_version = line.substr(first_space + 1);
        size_t second_space            = after_version.find(' ');
        if (second_space == std::string_view::npos)
            return slim::ErrorInfo(std::format("{} => response is unparsable", __func__));

        std::string_view code_sv = after_version.substr(0, second_space);
        int code;
        auto [ptr, ec] = std::from_chars(code_sv.data(), code_sv.data() + code_sv.size(), code);

        if (ec == std::errc::invalid_argument)
            return slim::ErrorInfo(static_cast<int>(ec), std::format("{} => response is unparsable => error code => {}", __func__, static_cast<int>(ec)));

        if (ec == std::errc::result_out_of_range)
            return slim::ErrorInfo(static_cast<int>(ec), std::format("{} => response code out of range => {} should be > 99 and < 600", __func__, code_sv));

        if (code < 100 || code > 599)
            return slim::ErrorInfo(std::format("{} => response code out of range => {} should be > 99  and < 600", __func__, code));

        out.version   = line.substr(0, first_space);
        out.code      = code;
        out.text      = after_version.substr(second_space + 1);
        headers_start = line_end + 2;
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
        r.code_text    = status.text;
        r.http_version = status.version;

        if (body_start < storage.size()) {
            auto body_span = storage.subspan(body_start);
            r.body = slim::slim_storage_container(body_span.begin(), body_span.end());
        }
    }
}

    Response::Response() {}

    Response::Response(std::span<const uint8_t> storage) {
        parse(*this, storage);
    }

    bool Response::has_error() const {
        return error_info.has_error();
    }

}