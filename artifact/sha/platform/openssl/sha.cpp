// Copyright 2023 Northern.tech AS
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#include <cstdint>
#include <string>
#include <ctime>
#include <iomanip>
#include <vector>
#include <sstream>
#include <algorithm>

#include <openssl/evp.h>
#include <artifact/sha/sha.hpp>

#include <common/common.hpp>


namespace mender {
namespace sha {

namespace log = mender::common::log;

const ErrorCategoryClass ErrorCategory = ErrorCategoryClass();

const char *ErrorCategoryClass::name() const noexcept {
	return "ShaSumErrorCategory";
}

string ErrorCategoryClass::message(int code) const {
	switch (code) {
	case NoError:
		return "Success";
	InitializationError:
		return "Initialization error";
	ShasumCreationError:
		return "Shasum creation error";
	ShasumMismatchError:
		return "Shasum mismatch error";
	default:
		return "Unknown";
	}
}

error::Error MakeError(ErrorCode code, const string &msg) {
	return error::Error(error_condition(code, ErrorCategory), msg);
}


Reader::Reader(io::Reader &reader) :
	Reader::Reader {reader, ""} {
}

Reader::Reader(io::Reader &reader, const std::string &expected_sha = "") :
	sha_handle_(EVP_MD_CTX_new(), [](EVP_MD_CTX *ctx) { EVP_MD_CTX_free(ctx); }),
	wrapped_reader_ {reader},
	expected_sha_ {expected_sha} {
	if (EVP_DigestInit_ex(sha_handle_.get(), EVP_sha256(), nullptr) != 1) {
		log::Error("Failed to initialize the shasummer");
		initialized_ = false;
		return;
	}
	initialized_ = true;
}

expected::ExpectedSize Reader::Read(
	vector<uint8_t>::iterator start, vector<uint8_t>::iterator end) {
	if (!initialized_) {
		return MakeError(InitializationError, "");
	}

	auto bytes_read = wrapped_reader_.Read(start, end);
	if (!bytes_read) {
		return bytes_read;
	}

	// bytes_read == 0 == EOF marker in our Reader/Writer interface implementation
	if (bytes_read.value() == 0) {
		auto real_sha = ShaSum();
		if (!real_sha) {
			return real_sha.error();
		}
		if (real_sha.value() != expected_sha_) {
			return MakeError(
				ShasumMismatchError,
				"The checksum of the read byte-stream does not match the expected checksum, (expected): "
					+ expected_sha_ + " (calculated): " + real_sha.value());
		}
		return 0;
	}

	if (EVP_DigestUpdate(sha_handle_.get(), &start[0], bytes_read.value()) != 1) {
		return MakeError(ShasumCreationError, "Failed to create the shasum");
	}

	return bytes_read.value();
}

expected::ExpectedString Reader::ShaSum() {
	vector<uint8_t> hash(EVP_MAX_MD_SIZE);
	unsigned int hash_length = 0;

	if (EVP_DigestFinal_ex(sha_handle_.get(), hash.data(), &hash_length) != 1) {
		return MakeError(ShasumCreationError, "Failed to create the shasum. OpenSSL error: ");
	}

	if (hash_length != 32) {
		return MakeError(
			ShasumCreationError,
			"SHA of unexpected length: " + std::to_string(hash_length) + " expected length: 32");
	}

	std::stringstream ss {};
	for (unsigned int i = 0; i < hash_length; ++i) {
		ss << std::hex << std::setw(2) << std::setfill('0') << (int) hash[i];
	}

	return ss.str();
}

} // namespace sha
} // namespace mender
