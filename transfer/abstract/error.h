/**
 * MIT License
 *
 * Copyright (c) 2026 Mag1c.H
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace ucm::transfer {

enum class ErrorCode : int32_t {
    Success = 0,

    UnsupportedProtocol = 1,
    UnsupportedTransfer = 2,
    InvalidAddress = 3,
    InvalidProtocol = 4,
    ResourceAllocationFailed = 5,
    StreamClosed = 6,
    StreamNotOpen = 7,
    InvalidTask = 8,
    TaskNotFound = 9,
    Cancelled = 10,
    Timeout = 11,
    NotSupported = 12,

    SourceNotFound = 100,
    SourcePermissionDenied = 101,
    SourceLocked = 102,
    SourceCorrupted = 103,
    SourceReadError = 104,
    SourceSeekError = 105,
    SourceEof = 106,

    DestinationNotFound = 200,
    DestinationPermissionDenied = 201,
    DestinationLocked = 202,
    DestinationFull = 203,
    DestinationWriteError = 204,
    DestinationSeekError = 205,

    ConnectionFailed = 300,
    ConnectionClosed = 301,
    ConnectionReset = 302,
    ConnectionRefused = 303,
    ConnectionTimeout = 304,
    DnsResolutionFailed = 305,
    SslHandshakeFailed = 306,
    CertificateError = 307,

    IoError = 400,
    PartialTransfer = 401,
    DataCorrupted = 402,
    ChecksumMismatch = 403,
    CompressionError = 404,
    DecompressionError = 405,
    EncodingError = 406,
    DecodingError = 407,
    BufferOverflow = 408,
    BufferUnderflow = 409,

    ProtocolError = 500,
    ProtocolVersionMismatch = 501,
    ProtocolViolation = 502,
    InvalidResponse = 503,
    UnexpectedMessage = 504,

    SystemError = 1000,
    OutOfMemory = 1001,
    ThreadError = 1002,
    SignalInterrupt = 1003,

    Unknown = 9999,
};

struct Error {
    ErrorCode code = ErrorCode::Success;
    std::string message;
    int32_t system_errno = 0;

    explicit operator bool() const { return code != ErrorCode::Success; }
    bool ok() const { return code == ErrorCode::Success; }
    bool is_system_error() const { return code == ErrorCode::SystemError; }
};

inline const char* to_string(ErrorCode code)
{
    switch (code) {
        case ErrorCode::Success: return "Success";
        case ErrorCode::UnsupportedProtocol: return "UnsupportedProtocol";
        case ErrorCode::UnsupportedTransfer: return "UnsupportedTransfer";
        case ErrorCode::InvalidAddress: return "InvalidAddress";
        case ErrorCode::InvalidProtocol: return "InvalidProtocol";
        case ErrorCode::ResourceAllocationFailed: return "ResourceAllocationFailed";
        case ErrorCode::StreamClosed: return "StreamClosed";
        case ErrorCode::StreamNotOpen: return "StreamNotOpen";
        case ErrorCode::InvalidTask: return "InvalidTask";
        case ErrorCode::TaskNotFound: return "TaskNotFound";
        case ErrorCode::Cancelled: return "Cancelled";
        case ErrorCode::Timeout: return "Timeout";
        case ErrorCode::NotSupported: return "NotSupported";
        case ErrorCode::SourceNotFound: return "SourceNotFound";
        case ErrorCode::SourcePermissionDenied: return "SourcePermissionDenied";
        case ErrorCode::SourceLocked: return "SourceLocked";
        case ErrorCode::SourceCorrupted: return "SourceCorrupted";
        case ErrorCode::SourceReadError: return "SourceReadError";
        case ErrorCode::SourceSeekError: return "SourceSeekError";
        case ErrorCode::SourceEof: return "SourceEof";
        case ErrorCode::DestinationNotFound: return "DestinationNotFound";
        case ErrorCode::DestinationPermissionDenied: return "DestinationPermissionDenied";
        case ErrorCode::DestinationLocked: return "DestinationLocked";
        case ErrorCode::DestinationFull: return "DestinationFull";
        case ErrorCode::DestinationWriteError: return "DestinationWriteError";
        case ErrorCode::DestinationSeekError: return "DestinationSeekError";
        case ErrorCode::ConnectionFailed: return "ConnectionFailed";
        case ErrorCode::ConnectionClosed: return "ConnectionClosed";
        case ErrorCode::ConnectionReset: return "ConnectionReset";
        case ErrorCode::ConnectionRefused: return "ConnectionRefused";
        case ErrorCode::ConnectionTimeout: return "ConnectionTimeout";
        case ErrorCode::DnsResolutionFailed: return "DnsResolutionFailed";
        case ErrorCode::SslHandshakeFailed: return "SslHandshakeFailed";
        case ErrorCode::CertificateError: return "CertificateError";
        case ErrorCode::IoError: return "IoError";
        case ErrorCode::PartialTransfer: return "PartialTransfer";
        case ErrorCode::DataCorrupted: return "DataCorrupted";
        case ErrorCode::ChecksumMismatch: return "ChecksumMismatch";
        case ErrorCode::CompressionError: return "CompressionError";
        case ErrorCode::DecompressionError: return "DecompressionError";
        case ErrorCode::EncodingError: return "EncodingError";
        case ErrorCode::DecodingError: return "DecodingError";
        case ErrorCode::BufferOverflow: return "BufferOverflow";
        case ErrorCode::BufferUnderflow: return "BufferUnderflow";
        case ErrorCode::ProtocolError: return "ProtocolError";
        case ErrorCode::ProtocolVersionMismatch: return "ProtocolVersionMismatch";
        case ErrorCode::ProtocolViolation: return "ProtocolViolation";
        case ErrorCode::InvalidResponse: return "InvalidResponse";
        case ErrorCode::UnexpectedMessage: return "UnexpectedMessage";
        case ErrorCode::SystemError: return "SystemError";
        case ErrorCode::OutOfMemory: return "OutOfMemory";
        case ErrorCode::ThreadError: return "ThreadError";
        case ErrorCode::SignalInterrupt: return "SignalInterrupt";
        case ErrorCode::Unknown: return "Unknown";
        default: return "UnknownErrorCode";
    }
}

template <typename T>
class Expected {
    std::optional<T> value_;
    Error error_;

public:
    Expected(T value) : value_(std::move(value)), error_{} {}
    Expected(Error error) : value_(std::nullopt), error_(std::move(error)) {}

    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }
    T& value() & { return *value_; }
    T&& value() && { return std::move(*value_); }
    const T& value() const& { return *value_; }
    T take_value() { return std::move(*value_); }
    const Error& error() const { return error_; }
};

}  // namespace ucm::transfer
