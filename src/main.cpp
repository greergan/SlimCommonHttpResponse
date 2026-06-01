#include <charconv>
#include <format>
#include <slim/common/http/response.h>

namespace slim::common::http {

    namespace {
        struct StatusLine {
            int              code;
            std::string_view version;
            std::string_view text;
        };

        slim::ErrorInfo parse_status_line(std::string_view raw, StatusLine& out, size_t& headers_start) {
            size_t line_end = raw.find("\r\n");
            if (line_end == std::string_view::npos)
                return slim::ErrorInfo("response is unparsable");

            std::string_view line = raw.substr(0, line_end);
            size_t first_space    = line.find(' ');
            if (first_space == std::string_view::npos)
                return slim::ErrorInfo("response is unparsable");

            size_t second_space = line.find(' ', first_space + 1);
            if (second_space == std::string_view::npos)
                return slim::ErrorInfo("response is unparsable");

            std::string_view code_sv = line.substr(first_space + 1, second_space - first_space - 1);
            int code;
            auto [ptr, ec] = std::from_chars(code_sv.data(), code_sv.data() + code_sv.size(), code);
            if (ec == std::errc::invalid_argument)
                return slim::ErrorInfo(static_cast<int>(ec), std::format("response is unparsable => error code => {}", static_cast<int>(ec)));
            if (ec == std::errc::result_out_of_range)
                return slim::ErrorInfo(static_cast<int>(ec), std::format("response code out of range => {} should be > 100 and < 512", code_sv));
            if (code < 100 || code > 511)
                return slim::ErrorInfo(std::format("response code out of range => {} should be > 100 and < 512", code));

            out.version   = line.substr(0, first_space);
            out.code      = code;
            out.text      = line.substr(second_space + 1);
            headers_start = line_end + 2;
            return slim::ErrorInfo{};
        }

        slim::ErrorInfo parse_headers(std::string_view raw, size_t headers_start, Headers& out, size_t& body_start) {
            size_t end_of_headers = raw.find("\r\n\r\n", headers_start);
            if (end_of_headers == std::string_view::npos)
                return slim::ErrorInfo("response is unparsable => headers not found");

            body_start = end_of_headers + 4;
            std::string_view raw_headers = raw.substr(headers_start, end_of_headers - headers_start);

            size_t pos = 0;
            while (pos < raw_headers.size()) {
                size_t line_end = raw_headers.find("\r\n", pos);
                size_t end      = (line_end == std::string_view::npos) ? raw_headers.size() : line_end;

                if (end > pos) {
                    std::string_view line = raw_headers.substr(pos, end - pos);
                    size_t colon          = line.find(':');
                    if (colon != std::string_view::npos) {
                        std::string_view key = line.substr(0, colon);
                        std::string_view val = line.substr(colon + 1);

                        while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
                            val.remove_prefix(1);
                        while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
                            val.remove_suffix(1);

                        auto result = out.set(key, val);
                        if (!result)
                            return result.get_error();
                    }
                }

                if (line_end == std::string_view::npos)
                    break;
                pos = line_end + 2;
            }

            return slim::ErrorInfo{};
        }
    }

    Response::Response() {}

    Response::Response(std::span<const uint8_t> storage) {
        parse(storage);
    }

    bool Response::has_error() const {
        return error_info.has_error();
    }

    void Response::parse(std::span<const uint8_t> storage) {
        if (storage.empty()) {
            error_info = slim::ErrorInfo("response storage is empty");
            return;
        }

        std::string_view raw(reinterpret_cast<const char*>(storage.data()), storage.size());

        StatusLine status;
        size_t headers_start;
        error_info = parse_status_line(raw, status, headers_start);
        if (error_info.has_error()) return;

        size_t body_start;
        error_info = parse_headers(raw, headers_start, headers, body_start);
        if (error_info.has_error()) return;

        code         = status.code;
        code_text    = status.text;
        http_version = status.version;

        if (body_start < storage.size()) {
            auto body_span = storage.subspan(body_start);
            body = slim::slim_storage_container(body_span.begin(), body_span.end());
        }
    }

}