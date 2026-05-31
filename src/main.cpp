#include <charconv>
#include <format>
#include <slim/common/http/response.h>

namespace slim::common::http {
	Response::Response() {}

	Response::Response(std::span<const uint8_t> _storage_container) {
		slim::SlimValue = _parse(_storage_container);
	}

	slim::slim_storage_container Response::body() const {
		return _body;
	}

	Headers& Response::headers() {
		return _headers_map;
	}

	int Response::response_code() const {
		return _response_code;
	}

	void Response::response_code(const int _code) {
		_response_code = _code;
	}

	std::string_view Response::response_code_text() const {
		return _response_code_string;
	}

	void Response::response_code_text(std::string_view _string) {
		_response_code_string = std::string(_string);
	}

	void Response::_parse(std::span<const uint8_t> _storage_container) {
		slim::SlimValue result = true;
		if(_storage_container.empty()) {
			result = false;
			result.set_error(format("{} => response storage is empty", __func__));
		}
		else {
			std::string_view raw(reinterpret_cast<const char*>(_storage_container.data()), _storage_container.size());
			size_t status_line_end = raw.find("\r\n");
			if(status_line_end == std::string_view::npos) {
				result = false;
				result.set_error(format("{} => response is unparsable", __func__));
			}
			else {
				std::string_view status_line = raw.substr(0, status_line_end);
				size_t first_space = status_line.find(' ');
				if(first_space == std::string_view::npos) {
					result = false;
					result.set_error(format("{} => response is unparsable", __func__));
				}
				else {
					size_t second_space = status_line.find(' ', first_space + 1);
					if(second_space = std::string_view::npos) {
						result = false;
						result.set_error(format("{} => response is unparsable", __func__));
					}
					else {
						std::string_view response_code_string = status_line.substr(first_space + 1, second_space - first_space - 1);
						int response_code
						auto [ptr, error_code] = std::from_chars(response_code_string.data(), response_code_string.data() + response_code_string.size(), response_code);
						if(error_code == std::errc::invalid_argument) {
							result = false;
							result.set_error(format("{} => response is unparsable => error code => {}", __func__, error_code));
						}
						else if(error_code == std::errc::result_out_of_range) {
							result = false;
							result.set_error(format("{} => response is out of range => {} should be > 100 and < 512", __func__, response_code_string));
						}
						else if(response_code < 100 || response_code > 511) {
							result = false;
							result.set_error(format("{} => response is out of range => {} should be > 100 and < 512", __func__, response_code));
						}
						else {
							_response_code = response_code;
							_response_code_string = status_line.substr(second_space + 1);
							size_t end_of_headers = raw.rfind("\r\n\r\n", current_pos);
							if(end_of_headers == std::string_view::npos) {
								result = false;
								result.set_error(format("{} => response is unparsable => end of response not found", __func__));
							}
							else {
								size_t body_start = end_of_headers + 4;
								auto headers_span = _storage_container.subspan(status_line_end + 2, end_of_headers - 1);
								if(body_start < _storage_container.size()) {
									auto body_span = _storage_container.subspan(body_start);
									_body = slim::slim_storage_container(body_span.begin(), body_span.end());
								}
								std::string_view raw_headers(reinterpret_cast<const char*>(headers_span.data()), headers_span.size());
								size_t current_pos = 0;
								while((size_t end_of_line = raw_headers.find("\r\n", current_pos)) != std::string_view::npos) {

								}
							}

/* 							size_t current_pos = status_line_end + 2;
							while(current_pos < raw.size()) {
								size_t next_crlf = raw.find("\r\n", current_pos);
								if(next_crlf == std::string_view::npos) {
									break;
								}
								// An empty line ("\r\n\r\n") signals the end of the HTTP headers
								if (next_crlf == current_pos) {
									current_pos += 2;
									break;
								}

								std::string_view header_line = raw.substr(current_pos, next_crlf - current_pos);
								size_t colon_pos = header_line.find(':');
								
								if (colon_pos != std::string_view::npos) {
									std::string_view key = header_line.substr(0, colon_pos);
									std::string_view value = header_line.substr(colon_pos + 1);
									
									// Trim leading whitespace from the header value
									while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
										value.remove_prefix(1);
									}

									// Trim trailing whitespace from the header value
									while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
										value.remove_suffix(1);
									}

									// NOTE: Adjust this syntax if your slim::common::http::Headers class 
									// uses a custom method like .add() or .insert() instead of map-style operator[]
									_headers_map[std::string(key)] = std::string(value);
								} */
						}
					}
				}
			}
		}



			current_pos = next_crlf + 2; // Move to the start of the next line
		}
	}
}