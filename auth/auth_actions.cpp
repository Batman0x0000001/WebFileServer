#include "auth_actions.h"

#include "auth.h"
#include "../utils/http_helpers.h"

RouteResult handleMeAction(Request &request, Response &response){
    std::string username;
    std::string token = getTokenFromCookie(request.msgHeader["Cookie"]);
    if(getUserByToken(token, username)){
        response.action = ResponseAction(ACTION_ME_SUCCESS_JSON, username);
    }else{
        response.action = ResponseAction(ACTION_ME_UNAUTHORIZED_JSON);
    }
    request.status = HANDLE_COMPLETE;
    return ROUTE_HANDLED;
}

RouteResult handleLogoutAction(Request &request, Response &response){
    std::string token = getTokenFromCookie(request.msgHeader["Cookie"]);
    deleteSessionToken(token);
    response.action = ResponseAction(ACTION_LOGOUT_SUCCESS_JSON);
    request.status = HANDLE_COMPLETE;
    return ROUTE_HANDLED;
}

RouteResult handleRegisterAction(Request &request, Response &response){
    if(request.recvMsg.size() < static_cast<size_t>(request.contentLength)){
        return ROUTE_NEED_MORE_BODY;
    }

    std::string body = request.recvMsg.substr(0, request.contentLength);
    request.recvMsg.erase(0, request.contentLength);
    auto formData = parseFormBody(body);
    std::string username = formData["username"];
    std::string password = formData["password"];

    if(!isValidUsername(username) || password.size() < 6){
        response.action = ResponseAction(ACTION_REGISTER_INVALID_JSON);
    }else if(!saveUser(username, password)){
        response.action = ResponseAction(ACTION_REGISTER_DUPLICATE_JSON);
    }else{
        response.action = ResponseAction(ACTION_REGISTER_SUCCESS_JSON);
    }

    request.status = HANDLE_COMPLETE;
    return ROUTE_HANDLED;
}

RouteResult handleLoginAction(Request &request, Response &response){
    if(request.recvMsg.size() < static_cast<size_t>(request.contentLength)){
        return ROUTE_NEED_MORE_BODY;
    }

    std::string body = request.recvMsg.substr(0, request.contentLength);
    request.recvMsg.erase(0, request.contentLength);
    auto formData = parseFormBody(body);
    std::string username = formData["username"];
    std::string password = formData["password"];

    if(checkUserPassword(username, password)){
        std::string token = createSessionToken(username);
        response.action = ResponseAction(ACTION_LOGIN_SUCCESS_JSON, token);
    }else{
        response.action = ResponseAction(ACTION_LOGIN_UNAUTHORIZED_JSON);
    }

    request.status = HANDLE_COMPLETE;
    return ROUTE_HANDLED;
}
