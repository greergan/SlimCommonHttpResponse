#if defined(SLIM_COMMON_HTTP_RESPONSE_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef SLIM_COMMON_HTTP_RESPONSE_INL_H_
#undef SLIM_COMMON_HTTP_RESPONSE_INL_H_
#else
#define SLIM_COMMON_HTTP_RESPONSE_INL_H_
#endif

#include "hwy/highway.h"

#include <format>
#include <slim/common/http/headers.h>
#include <slim/SlimValue.hpp>

HWY_BEFORE_NAMESPACE();
namespace slim::common::http {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

HWY_ATTR slim::ErrorInfo parse_headers(
    std::string_view raw,
    size_t           start,
    Headers&         out,
    size_t&          body_start)
{
    enum class State : uint8_t {
        scan_key,
        scan_value,
        seen_cr,
        seen_crlf_cr
    };

    const uint8_t* data = reinterpret_cast<const uint8_t*>(raw.data());
    const size_t   size = raw.size();

    const hn::ScalableTag<uint8_t> d;
    const size_t lanes  = hn::Lanes(d);
    const auto v_colon  = hn::Set(d, static_cast<uint8_t>(':'));
    const auto v_cr     = hn::Set(d, static_cast<uint8_t>('\r'));
    const auto v_lf     = hn::Set(d, static_cast<uint8_t>('\n'));
    const auto v_space  = hn::Set(d, static_cast<uint8_t>(' '));
    const auto v_tab    = hn::Set(d, static_cast<uint8_t>('\t'));

    State  state       = State::scan_key;
    size_t pos         = start;
    size_t key_start   = start;
    size_t key_end     = start;
    size_t value_start = start;

    auto emit_header = [&](size_t value_end) -> slim::ErrorInfo {
        std::string_view key   = raw.substr(key_start,   key_end   - key_start);
        std::string_view value = raw.substr(value_start, value_end - value_start);

        while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);

        auto result = out.set(key, value);
        if (!result)
            return result.get_error();
        return {};
    };

    while (pos < size) {
        if (pos + lanes <= size &&
            (state == State::scan_key ||
             state == State::scan_value)) {

            const auto chunk     = hn::LoadU(d, data + pos);
            const auto is_colon  = hn::Eq(chunk, v_colon);
            const auto is_cr     = hn::Eq(chunk, v_cr);
            const auto is_lf     = hn::Eq(chunk, v_lf);
            const auto is_ws     = hn::Or(hn::Eq(chunk, v_space), hn::Eq(chunk, v_tab));
            const auto is_any    = hn::Or(hn::Or(is_colon, is_cr), hn::Or(is_lf, is_ws));
            const intptr_t first = hn::FindFirstTrue(d, is_any);

            if (first < 0) {
                pos += lanes;
                continue;
            }

            pos += static_cast<size_t>(first);
        }

        if (pos >= size)
            break;

        const uint8_t byte = data[pos];

        switch (state) {
            case State::scan_key:
                if (byte == ':') {
                    key_end = pos;
                    ++pos;
                    while (pos < size && (data[pos] == ' ' || data[pos] == '\t'))
                        ++pos;
                    value_start = pos;
                    state       = State::scan_value;
                }
                else if (byte == '\r') {
                    state = State::seen_cr;
                    ++pos;
                }

                else if (byte == ' ' || byte == '\t') {
                    return slim::ErrorInfo(std::format(
                        "{} => obsolete line folding is not supported", __func__));
                }
                else {
                    ++pos;
                }
                break;

            case State::scan_value:
                if (byte == '\r') {
                    if (auto e = emit_header(pos); e.has_error()) return e;
                    state = State::seen_cr;
                    ++pos;
                }
                else {
                    ++pos;
                }
                break;

            case State::seen_cr:
                if (byte == '\n') {
                    ++pos;
                    if (pos < size && data[pos] == '\r') {
                        state = State::seen_crlf_cr;
                        ++pos;
                    }
                    else {
                        key_start = pos;
                        state     = State::scan_key;
                    }
                }
                else {
                    return slim::ErrorInfo(std::format("{} => bare CR in headers", __func__));
                }
                break;

            case State::seen_crlf_cr:
                if (byte == '\n') {
                    body_start = pos + 1;
                    return {};
                }
                else {
                    return slim::ErrorInfo(std::format("{} => malformed header terminator", __func__));
                }
        }
    }

    return slim::ErrorInfo(std::format("{} => headers not terminated", __func__));
}
}
}
HWY_AFTER_NAMESPACE();

#endif