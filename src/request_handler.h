// Based off given skeleton code: https://github.com/UCLA-CS130/webserver-api

#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include "config_parser.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

// Represents an HTTP Request.
//
// Usage:
//   auto request = Request::Parse(raw_request);
class Request {
 public:
    static std::unique_ptr<Request> Parse(const std::string& raw_request);

    virtual std::string raw_request() const;
    virtual std::string method() const;
    virtual std::string uri() const;
    std::string version() const;
    virtual std::string cookie() const;
    
    //New function to update header for reverse_proxy
    //If the header doesn't exist, it is added to the request
    void update_header(std::pair<std::string, std::string> header);
    void update_uri(std::string newUri);
    void setVersion(const std::string& version);

    using Headers = std::vector<std::pair<std::string, std::string>>;
    Headers headers() const;

    virtual std::string body() const;

 private:
    void update_raw_request();
  
    std::string raw_request_;
    std::string method_;
    std::string uri_;
    std::string version_;
    std::string cookie_;
    std::vector<std::pair<std::string, std::string>> headers_;
    std::string body_;
};

// Represents an HTTP response.
//
// Usage:
//   Response r;
//   r.SetStatus(RESPONSE_200);
//   r.SetBody(...);
//   return r.ToString();
//
// Constructed by the RequestHandler, after which the server should call ToString
// to serialize.
class Response {
 public:
    enum ResponseCode {
        OK = 200,
        MOVED_PERMANENTLY = 301,
        FOUND = 302,
        BAD_REQUEST = 400,
        NOT_FOUND = 404,
        INTERNAL_SERVER_ERROR = 500,
        NOT_IMPLEMENTED = 501
    };

    Response& operator=(const Response& rhs);
    static std::unique_ptr<Response> Parse(const std::string& raw_response);

    void SetStatus(const ResponseCode response_code);
    void AddHeader(const std::string& header_name, const std::string& header_value);
    void SetBody(const std::string& body);
    bool convertCode(const int& code, ResponseCode& rc);
    
    std::string GetHeader(const std::string& headerName);
    std::string ToString();
    ResponseCode status_code();
    
    void PrintHeaders();
 private:
    ResponseCode status_code_;
    std::string raw_response_;
    std::string version_;
    std::string status_;
    std::string response_body_;
    std::vector<std::pair<std::string, std::string>> headers_;
};

// Represents the parent of all request handlers. Implementations should expect to
// be long lived and created at server constrution.
class RequestHandler {
 public:
    enum Status {
        OK = 0,
        INVALID_CONFIG = 1,
        INVALID_URI = 2,
        FILE_NOT_FOUND = 3,
        PROXY_ERROR = 4,
        DATABASE_ERROR = 5
    };

    // Initializes the handler. Returns true if successful.
    // uri_prefix is the value in the config file that this handler will run for.
    // config is the contents of the child block for this handler ONLY.
    virtual Status Init(const std::string& uri_prefix,
                        const NginxConfig& config) = 0;

    // Handles an HTTP request, and generates a response. Returns a response code
    // indicating success or failure condition. If ResponseCode is not OK, the
    // contents of the response object are undefined, and the server will return
    // HTTP code 500.
    virtual Status HandleRequest(const Request& request,
                                 Response* response) = 0;

    static RequestHandler* CreateByName(const char* type);
};

// Notes:
// * The trick here is that you can declare an object at file scope, but you
//   can't do anything else, such as set a map key. But you can get around this
//   by creating a class that does work in its constructor.
// * request_handler_builders must be a pointer. Otherwise, it won't necessarily
//   exist when the RequestHandlerRegisterer constructor gets called.

extern std::map<std::string, RequestHandler* (*)(void)>* request_handler_builders;
template<typename T>
class RequestHandlerRegisterer {
 public:
    RequestHandlerRegisterer(const std::string& type) {
        if (request_handler_builders == nullptr) {
            request_handler_builders = new std::map<std::string, RequestHandler* (*)(void)>;
        }
        (*request_handler_builders)[type] = RequestHandlerRegisterer::Create;
    }
    static RequestHandler* Create() {
        return new T;
    }
};
#define REGISTER_REQUEST_HANDLER(ClassName) \
    static RequestHandlerRegisterer<ClassName> ClassName##__registerer(#ClassName)

#endif  // REQUEST_HANDLER_H
