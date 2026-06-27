#define HWY_TARGET_INCLUDE "slim/common/http/response-inl.h"
#include "hwy/foreach_target.h"
#undef HWY_TARGET_INCLUDE
#include "slim/common/http/response-inl.h"

#include <charconv>
#include <slim/common/http/headers.h>
#include <slim/common/http/response.h>
#include <slim/common/utilities.h>

namespace slim::common::http {

HWY_EXPORT(parse_headers);

namespace {
using slim::common::utilities::iequals;
using slim::common::utilities::trim;
using slim::common::utilities::is_space;

ErrorStatus decode_chunked_body(std::span<const uint8_t> buf, size_t cursor, std::vector<uint8_t>& out_body) {
    if (out_body.empty()) out_body.reserve(buf.size() - cursor);

    while (cursor < buf.size()) {
        std::string_view rem(reinterpret_cast<const char*>(buf.data() + cursor), buf.size() - cursor);
        size_t line_end = rem.find("\r\n");
        if (line_end == std::string_view::npos) return ErrorStatus::ResponseChunkedMissingCRLF;

        std::string_view chunk_line = rem.substr(0, line_end);
        size_t ext = chunk_line.find(';');
        if (ext != std::string_view::npos) chunk_line = chunk_line.substr(0, ext);
        trim(chunk_line);

        size_t chunk_size = 0;
        auto [ptr, ec] = std::from_chars(chunk_line.data(), chunk_line.data() + chunk_line.size(), chunk_size, 16);
        if (ec != std::errc{}) return ErrorStatus::ResponseChunkedSizeInvalid;
        if (chunk_size == 0) break;

        cursor += line_end + 2;
        if (cursor + chunk_size > buf.size()) return ErrorStatus::ResponseChunkedTruncated;

        out_body.insert(out_body.end(), buf.data() + cursor, buf.data() + cursor + chunk_size);
        cursor += chunk_size;

        if (cursor + 2 > buf.size() || buf[cursor] != '\r' || buf[cursor + 1] != '\n')
            return ErrorStatus::ResponseChunkedMissingCRLFAfterData;
        cursor += 2;
    }

    return ErrorStatus::OK;
}

} // namespace

Response::Response(std::span<uint8_t> buf) {
    if (auto e = parse(buf); e != ErrorStatus::OK) throw ResponseParseException(e);
}

ErrorStatus Response::parse(std::span<const uint8_t> buf) {
    if (buf.empty()) return ErrorStatus::ResponseStorageEmpty;

    std::string_view s(reinterpret_cast<const char*>(buf.data()), buf.size());
    size_t headers_start;
    if (auto e = parse_status_line(s, headers_start); e != ErrorStatus::OK) return e;

    size_t body_start = buf.size();
    if (auto e = HWY_DYNAMIC_DISPATCH(parse_headers)(s, headers_start, headers, body_start); e != ErrorStatus::OK) return e;

    bool is_chunked = false;
    if (auto te_h = headers.get("transfer-encoding"); te_h) {
        const auto& values = te_h->get_value();
        if (!values.empty() && iequals(values.back(), "chunked")) is_chunked = true;
    }

    if (is_chunked) return decode_chunked_body(buf, body_start, body);
    else {
        if (auto cl_h = headers.get("content-length"); cl_h) {
            const auto& values = cl_h->get_value();
            if (values.empty()) return ErrorStatus::ResponseContentLengthInvalid;

            std::string_view value = values[0];
            size_t len = 0;
            if (auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), len); ec != std::errc{})
                return ErrorStatus::ResponseContentLengthInvalid;

            if (buf.size() - body_start < len) return ErrorStatus::ResponseContentLengthMismatch;

            if (len > 0) {
                body.reserve(body.size() + len);
                body.insert(body.end(), buf.data() + body_start, buf.data() + body_start + len);
            }
        }
        else if (body_start < buf.size()) {
            size_t len = buf.size() - body_start;
            body.reserve(body.size() + len);
            body.insert(body.end(), buf.data() + body_start, buf.data() + buf.size());
        }
    }

    return ErrorStatus::OK;
}

ErrorStatus Response::parse_status_line(std::string_view s, size_t& headers_start) {
    // version
    size_t pos = s.find(' ');
    if (pos == std::string_view::npos) return ErrorStatus::ResponseStatusLineInvalid;
    version = s.substr(0, pos);

    // status code
    size_t code_start = pos + 1;
    size_t code_end = s.find_first_of(" \r", code_start);
    if (code_end == std::string_view::npos) return ErrorStatus::ResponseStatusLineInvalid;

    auto [ptr, ec] = std::from_chars(s.data() + code_start, s.data() + code_end, code);
    if (ec != std::errc{}) return ErrorStatus::ResponseStatusCodeInvalid;
    if (code < 100 || code > 599) return ErrorStatus::ResponseStatusCodeOutOfRange;

    // status code text
    size_t line_end = s.find("\r\n", code_end);
    if (line_end == std::string_view::npos) return ErrorStatus::ResponseStatusLineInvalid;

    size_t code_text_start = (s[code_end] == ' ') ? code_end + 1 : code_end;
    size_t code_text_end = line_end;
    while (code_text_end > code_text_start && is_space(s[code_text_end - 1])) --code_text_end;

    code_text = s.substr(code_text_start, code_text_end - code_text_start);
    headers_start = line_end + 2;
    return ErrorStatus::OK;
}
} // namespace slim::common::http
