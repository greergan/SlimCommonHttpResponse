#define HWY_TARGET_INCLUDE "slim/common/http/response-inl.h"
#include "hwy/foreach_target.h"
#undef HWY_TARGET_INCLUDE
#include "slim/common/http/response-inl.h"

#include <charconv>
#include <vector>
#include <slim/common/http/headers.h>
#include <slim/common/http/response.h>
#include <slim/common/utilities.h>

namespace slim::common::http {

HWY_EXPORT(parse_headers);

namespace {
using slim::common::utilities::iequals;
using slim::common::utilities::trim;

ErrorStatus decode_chunked_body(std::span<const uint8_t> buf, size_t cursor, std::vector<uint8_t>& out_body) {
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

ErrorStatus parse_status_line(std::string_view s, Response& r, size_t& headers_start) {
    size_t pos = 0;

    // version
    size_t version_start = pos;
    while (pos < s.size() && s[pos] != ' ' && s[pos] != '\r') ++pos;
    if (pos >= s.size() || s[pos] != ' ') return ErrorStatus::ResponseStatusLineInvalid;
    r.version(s.substr(version_start, pos - version_start));

    // status code
    ++pos;
    size_t code_start = pos;
    while (pos < s.size() && s[pos] != ' ' && s[pos] != '\r') ++pos;
    if (pos >= s.size()) return ErrorStatus::ResponseStatusLineInvalid;

    int code = 0;
    auto [ptr, ec] = std::from_chars(s.data() + code_start, s.data() + pos, code);
    if (ec != std::errc{})        return ErrorStatus::ResponseStatusCodeInvalid;
    if (code < 100 || code > 599) return ErrorStatus::ResponseStatusCodeOutOfRange;
    r.code(code);

    // status code text
    size_t code_text_start = (s[pos] == ' ') ? ++pos : pos;
    while (pos < s.size() - 1 && !(s[pos] == '\r' && s[pos + 1] == '\n')) ++pos;
    if (pos >= s.size() - 1) return ErrorStatus::ResponseStatusLineInvalid;

    size_t code_text_end = pos;
    while (code_text_end > code_text_start && (s[code_text_end - 1] == ' ' || s[code_text_end - 1] == '\t')) --code_text_end;

    r.code_text(s.substr(code_text_start, code_text_end - code_text_start));
    headers_start = pos + 2;
    return ErrorStatus::OK;
}

ErrorStatus parse(Response& r, std::span<const uint8_t> buf) {
    if (buf.empty()) return ErrorStatus::ResponseStorageEmpty;

    std::string_view s(reinterpret_cast<const char*>(buf.data()), buf.size());
    size_t headers_start;
    if (auto e = parse_status_line(s, r, headers_start); e != ErrorStatus::OK) return e;

    size_t body_start = buf.size();
    if (auto e = HWY_DYNAMIC_DISPATCH(parse_headers)(s, headers_start, r.headers(), body_start); e != ErrorStatus::OK) return e;

    bool is_chunked = false;
    if (auto te_h = r.headers().get("transfer-encoding"); te_h) {
        const auto& values = te_h->get_value();
        if (!values.empty() && iequals(values.back(), "chunked")) is_chunked = true;
    }

    if (is_chunked) return decode_chunked_body(buf, body_start, r.body());
    else {
        if (auto cl_h = r.headers().get("content-length"); cl_h) {
            std::string_view value = cl_h->get_value()[0];
            size_t len = 0;
            if (auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), len); ec != std::errc{})
                return ErrorStatus::ResponseContentLengthInvalid;

            if (buf.size() - body_start < len) return ErrorStatus::ResponseContentLengthMismatch;

            if (len > 0) {
                auto body_span = buf.subspan(body_start, len);
                auto& dest = r.body();
                dest.reserve(dest.size() + body_span.size());
                dest.insert(dest.end(), body_span.begin(), body_span.end());
            }
        }
        else if (body_start < buf.size()) {
            auto body_span = buf.subspan(body_start);
            auto& dest = r.body();
            dest.reserve(dest.size() + body_span.size());
            dest.insert(dest.end(), body_span.begin(), body_span.end());
        }
    }

    return ErrorStatus::OK;
}
} // namespace

    Response::Response(std::span<uint8_t> buf) {
        if (auto e = parse(*this, buf); e != ErrorStatus::OK) throw ResponseParseException(e);
    }

} // namespace slim::common::http
