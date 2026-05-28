#include <cassert>
#include <iostream>

#include "../auth/auth.h"
#include "../message/http_parser.h"
#include "../utils/encoding.h"
#include "../utils/http_helpers.h"

int main(){
    Request request;
    assert(HttpRequestParser::setRequestLine(request, "GET /download/a.txt?v=1 HTTP/1.1\r\n"));
    assert(request.requestMethod == "GET");
    assert(request.requestResourse == "/download/a.txt");
    assert(request.queryString == "v=1");
    assert(request.httpVersion == "HTTP/1.1");
    assert(HttpRequestParser::validateRequestLine(request));
    assert(HttpRequestParser::isSupportedMethod(request.requestMethod));

    assert(!HttpRequestParser::isSupportedMethod("DELETE"));
    assert(!HttpRequestParser::validateHeaders(request));

    assert(HttpRequestParser::addHeaderOpt(request, "Host: 127.0.0.1:8888\r\n"));
    assert(HttpRequestParser::validateHeaders(request));

    Request badVersion;
    assert(HttpRequestParser::setRequestLine(badVersion, "GET / HTTP/1.0\r\n"));
    assert(!HttpRequestParser::validateRequestLine(badVersion));

    Request emptyHeaderValue;
    assert(HttpRequestParser::addHeaderOpt(emptyHeaderValue, "Cookie:\r\n"));
    assert(emptyHeaderValue.msgHeader["Cookie"] == "");
    assert(!HttpRequestParser::addHeaderOpt(emptyHeaderValue, ":\r\n"));

    assert(urlDecode("hello+world%21") == "hello world!");
    assert(urlEncode("a b") == "a%20b");
    assert(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb924"
                            "27ae41e4649b934ca495991b7852b855");
    assert(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223"
                               "b00361a396177a9cb410ff61f20015ad");
    std::string passwordHash = hashPassword("secret123");
    assert(passwordHash.find("pbkdf2_sha256$") == 0);
    assert(verifyPassword("secret123", passwordHash));
    assert(!verifyPassword("wrong", passwordHash));
    assert(verifyPassword("secret123", "sha256$salt$" + sha256Hex("saltsecret123")));
    assert(isValidUsername("abc_123"));
    assert(!isValidUsername("ab"));
    assert(!isValidUsername("abc-123"));
    assert(isSafeFilename("report.txt"));
    assert(!isSafeFilename(""));
    assert(!isSafeFilename("../report.txt"));
    assert(!isSafeFilename("dir/report.txt"));
    assert(!isSafeFilename("report..txt"));
    auto formData = parseFormBody("username=alice&password=a%2Bb&empty=");
    assert(formData["username"] == "alice");
    assert(formData["password"] == "a+b");
    assert(formData["empty"] == "");
    assert(getTokenFromCookie("foo=bar; token=abc123; theme=light") == "abc123");
    assert(getTokenFromCookie("foo=bar") == "");

    Request parsed;
    parsed.recvMsg = "GET /?page=1 HTTP/1.1\r\nHost: 127.0.0.1:8888\r\nConnection: keep-alive\r\nCookie: token=abc\r\n\r\n";
    assert(HttpRequestParser::parseRequestLine(parsed) == HttpRequestParser::PARSE_OK);
    assert(parsed.requestMethod == "GET");
    assert(parsed.requestResourse == "/");
    assert(parsed.queryString == "page=1");
    assert(HttpRequestParser::parseHeaders(parsed) == HttpRequestParser::PARSE_OK);
    assert(parsed.msgHeader["Host"] == "127.0.0.1:8888");
    assert(parsed.msgHeader["Cookie"] == "token=abc");
    assert(parsed.keepAlive);
    assert(parsed.status == HANDLE_BODY);

    Request multipart;
    multipart.recvMsg = "POST / HTTP/1.1\r\nhost: 127.0.0.1:8888\r\ncontent-type: multipart/form-data;boundary=\"qt-boundary\"\r\ncontent-length: 10\r\naccept: application/json\r\n\r\n";
    assert(HttpRequestParser::parseRequestLine(multipart) == HttpRequestParser::PARSE_OK);
    assert(HttpRequestParser::parseHeaders(multipart) == HttpRequestParser::PARSE_OK);
    assert(multipart.msgHeader["Host"] == "127.0.0.1:8888");
    assert(multipart.msgHeader["Content-Type"] == "multipart/form-data");
    assert(multipart.msgHeader["boundary"] == "qt-boundary");
    assert(multipart.msgHeader["Accept"] == "application/json");
    assert(multipart.contentLength == 10);

    Request incomplete;
    incomplete.recvMsg = "GET / HTTP/1.1";
    assert(HttpRequestParser::parseRequestLine(incomplete) == HttpRequestParser::PARSE_NEED_MORE);

    Request missingHost;
    missingHost.recvMsg = "GET / HTTP/1.1\r\n\r\n";
    assert(HttpRequestParser::parseRequestLine(missingHost) == HttpRequestParser::PARSE_OK);
    assert(HttpRequestParser::parseHeaders(missingHost) == HttpRequestParser::PARSE_BAD_REQUEST);

    Request unsupported;
    unsupported.recvMsg = "DELETE / HTTP/1.1\r\n";
    assert(HttpRequestParser::parseRequestLine(unsupported) == HttpRequestParser::PARSE_METHOD_NOT_ALLOWED);

    Request chunked;
    chunked.recvMsg = "POST /upload HTTP/1.1\r\nHost: 127.0.0.1:8888\r\nTransfer-Encoding: gzip, chunked\r\n\r\n";
    assert(HttpRequestParser::parseRequestLine(chunked) == HttpRequestParser::PARSE_OK);
    assert(HttpRequestParser::parseHeaders(chunked) == HttpRequestParser::PARSE_NOT_IMPLEMENTED);

    std::cout << "http_parser_test passed" << std::endl;
    return 0;
}
