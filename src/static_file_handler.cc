#include "static_file_handler.h"
#include "../cpp-markdown/markdown.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <time.h>
#include <random>
#include <unordered_map>

// Get the content type from the file name.
std::string StaticFileHandler::get_content_type(const std::string& filename_str) {
    // Find last period.
    size_t pos = filename_str.find_last_of('.');
    std::string type;

    // No file extension.
    if (pos == std::string::npos || (pos+1) >= filename_str.size()) {
        return TYPE_OCT;
    }

    // Type = everything after last '.'
    type = filename_str.substr(pos+1);

    // Change to all lowercase.
    std::transform(type.begin(), type.end(), type.begin(), ::tolower);

    // Get file type.
    if (type == "jpeg" || type == "jpg") {
        return TYPE_JPEG;
    } else if (type == "gif") {
        return TYPE_GIF;
    } else if (type == "html" || type=="htm") {
        return TYPE_HTML;
    } else if (type == "pdf") {
        return TYPE_PDF;
    } else if (type == "png") {
        return TYPE_PNG;
    } else if (type == "txt") {
        return TYPE_TXT;
    } else if (type == "md") {
        return TYPE_MD;
    } else { // Default to type octet-stream.
        return TYPE_OCT;
    }
}

RequestHandler::Status StaticFileHandler::Init(const std::string& uri_prefix, const NginxConfig& config) {
    prefix = uri_prefix;
    root = "";
    timeout = 0;

    // Iterate through the config block to find the root mapping.
    for (size_t i = 0; i < config.statements_.size(); i++) {
        std::shared_ptr<NginxConfigStatement> stmt = config.statements_[i];

        size_t size = stmt->tokens_.size();
        std::string first_token = "";
        std::string second_token = "";
        std::string third_token = "";

        // Check statement size
        if (size > 1 && size < 4) {
            first_token = stmt->tokens_[0];
            second_token = stmt->tokens_[1];
            if (size == 3) {
                third_token = stmt->tokens_[2];
            }
        }
        else {
            // Error: The root value has already been set.
            std::cerr << "Error: Incorrect statement size for " << uri_prefix <<".\n";
            return RequestHandler::Status::INVALID_CONFIG;
        }

        if (first_token == "root" && third_token == "") {
            // Set the root value.
            if (root.empty()) {
                root = second_token;
                if (root.back() == '/') {
                    // Remove trailing slash from root
                    root = root.substr(0, root.length()-1);
                }
            } else {
                // Error: The root value has already been set.
                std::cerr << "Error: Multiple root mappings specified for " << uri_prefix <<".\n";
                return RequestHandler::Status::INVALID_CONFIG;
            }
        } else if (first_token == "user" && third_token != "") {
            std::unordered_map<std::string, std::string>::const_iterator found = user_map.find(second_token);

            // Check if user already exists
            if (found == user_map.end()) {
                user_map[second_token] = third_token;
            } else {
                // Error: The user has already been set.
                std::cerr << "Error: Multiple user mappings specified for " << uri_prefix <<".\n";
                return RequestHandler::Status::INVALID_CONFIG;
            }
        } else if (first_token == "timeout" && third_token == "") {
            // Set the timeout value.
            if (timeout == 0) {
                bool is_number = (stmt->tokens_[1].find_first_not_of("1234567890") == std::string::npos);
                if (is_number) {
                    timeout = std::stoi(second_token);
                } else {
                    // Error: The timeout is not a number.
                    std::cerr << "Error: Timeout is not a number.\n";
                    return RequestHandler::Status::INVALID_CONFIG;
                }
            } else {
                // Error: The timeout value has already been set.
                std::cerr << "Error: Multiple timeout mappings specified for " << uri_prefix <<".\n";
                return RequestHandler::Status::INVALID_CONFIG;
            }
        }
    }

    if (root.empty()) {
        // Error: No config definition for the root.
        std::cerr << "Error: No root mapping specified for " << uri_prefix <<".\n";
        return RequestHandler::Status::INVALID_CONFIG;
    }

    if ((user_map.size() == 0 || timeout < 1) && !(user_map.size() == 0 && timeout < 1)) {
        // Error: Either all initialized or uninitialized.
        std::cerr << "Error: The users and timeout need to be all initialized or all uninitialized.\n";
        std::cerr << uri_prefix;
        return RequestHandler::Status::INVALID_CONFIG;
    }

    return RequestHandler::Status::OK;
}

RequestHandler::Status StaticFileHandler::HandleRequest(const Request& request, Response* response) {
    std::string file_path = "";
    std::string contents = "";
    std::string login = prefix + "/login.html";
    bool redirect = false;

    // Get URI.
    std::string filename = request.uri();

    // Check if URI is login page
    if (filename.find(login) == 0 && login.length() == filename.length()) {
        redirect = true;
    }

    // If serving regular static files or the request is about login.html, skip
    if (timeout != 0 && !redirect) {
        bool cookie_ok = check_cookie(request.cookie(), response);

        if (!cookie_ok) {
            // If cookie is not ok, need to redirect
            original_uri = filename;
            return RequestHandler::Status::OK;
        }
    }

    if (request.method() == "POST" && redirect) {
        // The body returns: username=USERNAME&password=PASSWORD
        // Extract username and password
        std::string body = request.body();
        size_t first = body.find("=") + 1;
        size_t second = body.find("&");
        std::string user = body.substr(first, second - first);
        size_t third = body.find("=", second);
        std::string pass = body.substr(third + 1);

        std::unordered_map<std::string, std::string>::const_iterator found = user_map.find(user);

        if (found != user_map.end() && pass == found->second) {
            // Generate and then add the cookie to cookie_map
            std::string new_cookie = add_cookie(request.cookie());

            // Redirect to the original url and set the cookie
            // If the original request was login.html, don't redirect
            if (original_uri !=  "") {
                response->AddHeader("Location", original_uri);
            }

            response->AddHeader("Set-Cookie", "private=" + new_cookie);
        }

        original_uri = "";
    }

    // Check the URI prefix.
    if (filename.find(prefix) != 0) {
        // Something is wrong, the prefix does not match the URI.
        response = nullptr;
        std::cout << "StaticFileHandler: prefix does not match uri" << std::endl;
        return RequestHandler::Status::INVALID_URI;
    } else {
        // Remove the prefix from the URI to get the file name.
        filename.erase(0, prefix.length());
    }

    // If no file, do not try to open directory.
    if (filename.empty() || filename == "/") {
        response = nullptr;
        std::cout << "StaticFileHandler: Empty file name" << std::endl;
        return RequestHandler::Status::FILE_NOT_FOUND;
    }

    // Open file
    file_path = root + filename;
    std::cout << "StaticFileHandler: Handling request for " + file_path << std::endl;
    Response::ResponseCode response_code = get_file(file_path, &contents);

    if (response_code != Response::ResponseCode::OK) {
        response = nullptr;
        std::cout << "StaticFileHandler: File not found: " + file_path << std::endl;
        return RequestHandler::Status::FILE_NOT_FOUND;
    }

    // Create response headers

    // If There is a redirect, set the response code
    if (response->GetHeader("Location") == "") {
        response->SetStatus(response_code);
    } else {
        response->SetStatus(Response::ResponseCode::FOUND);
    }

    std::string content_type = get_content_type(filename);
    //check for markdown type before setting to html
    if (content_type == "text/markdown") {
        response->AddHeader("Content-Type", "text/html");
    } else {
        response->AddHeader("Content-Type", content_type);
        response->AddHeader("Content-Length", std::to_string(contents.length()));
    }
    // Set response body
    if (content_type == "text/markdown") {
        markdown::Document doc;
        doc.read(contents);
        std::ostringstream stream;
        doc.write(stream);
        std::string markdown = stream.str();
        //add github styling to markdown
        markdown.insert(0,
            "<link rel=\"stylesheet\" href=\"markdown.css\">"
            "<style>"
                ".markdown-body {"
                   "box-sizing: border-box;"
                    "min-width: 200px;"
                    "max-width: 980px;"
                    "margin: 0 auto;"
                    "padding: 45px;"
                "}"
            "</style>"
            "<body class=\"markdown-body\">");
        markdown.append("</body>");
        //add content length after html is generated
        response->AddHeader("Content-Length", std::to_string(markdown.length()));
        response->SetBody(markdown);

    } else {
        response->SetBody(contents);
    }

    return RequestHandler::Status::OK;
}

Response::ResponseCode StaticFileHandler::get_file(const std::string& file_path, std::string* contents) {
    // Attempt to open file
    std::ifstream in_stream(file_path.c_str(), std::ios::in | std::ios::binary);

    // File doesn't exist
    if (!in_stream) {
        contents = nullptr;
        return Response::ResponseCode::NOT_FOUND;
    }

    // Read file contents
    std::stringstream sstr;
    sstr << in_stream.rdbuf();
    contents->assign(sstr.str());

    return Response::ResponseCode::OK;
}

// Taken from: http://stackoverflow.com/a/24586587
// Generate a random alphanumeric string of any length
std::string StaticFileHandler::gen_cookie(std::string::size_type length)
{
    static auto& chrs = "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    thread_local static std::mt19937 rg{std::random_device{}()};
    thread_local static std::uniform_int_distribution<std::string::size_type> pick(0, sizeof(chrs) - 2);

    std::string s;

    s.reserve(length);

    while(length--)
        s += chrs[pick(rg)];

    return s;
}

std::string StaticFileHandler::add_cookie(std::string old_cookie) {
    std::unordered_map<std::string, time_t>::const_iterator found = cookie_map.find(old_cookie);

    // If the cookie already exists, but the user enters username and password again, delete the old cookie
    if (found != cookie_map.end()) {
        cookie_map.erase(found);
    }

    // Create a new cookie
    std::string new_cookie = gen_cookie(20);
    found = cookie_map.find(new_cookie);

    time_t now_seconds;
    now_seconds = time(NULL);

    // Add cookie to map, if duplicate created, create again and add
    if (found == cookie_map.end()) {
        cookie_map[new_cookie] = now_seconds;
    } else {
        new_cookie = gen_cookie(20);
        cookie_map[new_cookie] = now_seconds;
    }

    return new_cookie;
}

bool StaticFileHandler::check_cookie(std::string cookie, Response* response) {
    time_t cookie_time = 0;
    std::unordered_map<std::string, time_t>::const_iterator found;

    // Find cookie in the cookie map and get time
    if (cookie != "") {
        found = cookie_map.find(cookie);
        if (found != cookie_map.end()) {
            cookie_time = found->second;
        }
    }

    time_t now_seconds;
    now_seconds = time(NULL);

    if (now_seconds - cookie_time > timeout) {
        // If no cookie or expired, redirect to login and delete old cookie
        if (found != cookie_map.end()) {
            cookie_map.erase(found);
        }

        response->SetStatus(Response::ResponseCode::FOUND);
        response->AddHeader("Location", prefix + "/login.html");
        response->AddHeader("Set-Cookie", "private=" + cookie + "; expires=Thu, Jan 01 1970 00:00:00 UTC;");
        response->AddHeader("Content-Type", "text/html");
        response->AddHeader("Content-Length", "228");
        std::string contents = "";
        get_file(root + "/login.html", &contents);
        response->SetBody(contents);
        return false;
    }

    return true;
}
