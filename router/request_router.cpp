#include "request_router.h"

#include "../auth/auth.h"
#include "../auth/auth_actions.h"
#include "../utils/http_helpers.h"
#include "../upload/multipart_upload.h"

namespace{

RouteResult routeGet(Request &request, Response &response){
    if(request.requestResourse == "/api/me"){
        return handleMeAction(request, response);
    }
    if(request.requestResourse == "/api/logout"){
        return handleLogoutAction(request, response);
    }
    if(request.requestResourse == "/api/files"){
        // 文件列表是登录用户私有资源，未登录统一返回 JSON 401。
        if(!getLoginUserFromCookie(request.msgHeader["Cookie"], response.username)){
            response.action = ResponseAction(ACTION_ME_UNAUTHORIZED_JSON);
        }else{
            response.action = ResponseAction(ACTION_FILE_LIST_JSON);
        }
        request.status = HANDLE_COMPLETE;
        return ROUTE_HANDLED;
    }

    if(!getLoginUserFromCookie(request.msgHeader["Cookie"], response.username)){
        response.action = ResponseAction(ACTION_ME_UNAUTHORIZED_JSON);
    }else if(request.requestResourse.find("/download/") == 0){
        // 下载的文件名仍保持 URL 编码，响应构建时再解码和安全校验。
        response.action = ResponseAction(ACTION_DOWNLOAD, request.requestResourse.substr(std::string("/download/").size()));
    }else{
        response.action = ResponseAction(ACTION_NOT_FOUND);
    }
    request.status = HANDLE_COMPLETE;
    return ROUTE_HANDLED;
}

RouteResult routePostForm(Request &request, Response &response){
    if(request.requestResourse == "/api/register"){
        return handleRegisterAction(request, response);
    }
    if(request.requestResourse == "/api/login"){
        return handleLoginAction(request, response);
    }
    if(request.requestResourse == "/api/delete"){
        if(!getLoginUserFromCookie(request.msgHeader["Cookie"], response.username)){
            response.action = ResponseAction(ACTION_ME_UNAUTHORIZED_JSON);
            request.status = HANDLE_COMPLETE;
            return ROUTE_HANDLED;
        }
        if(request.recvMsg.size() < static_cast<size_t>(request.contentLength)){
            return ROUTE_NEED_MORE_BODY;
        }

        // 删除动作只路由到数据库元信息删除，磁盘文件由响应层服务保持不动。
        std::string body = request.recvMsg.substr(0, request.contentLength);
        request.recvMsg.erase(0, request.contentLength);
        auto formData = parseFormBody(body);
        response.action = ResponseAction(ACTION_DELETE_JSON, formData["filename"]);
        request.status = HANDLE_COMPLETE;
        return ROUTE_HANDLED;
    }
    return ROUTE_NOT_HANDLED;
}

RouteResult routePostMultipart(Request &request, Response &response, int clientFd){
    if(request.msgHeader["Content-Type"] != "multipart/form-data"){
        return ROUTE_NOT_HANDLED;
    }

    if(!getLoginUserFromCookie(request.msgHeader["Cookie"], response.username)){
        response.action = ResponseAction(ACTION_ME_UNAUTHORIZED_JSON);
        request.status = HANDLE_COMPLETE;
        return ROUTE_HANDLED;
    }

    MultipartUploadResult uploadResult = processMultipartUpload(request, response.username, clientFd);
    if(uploadResult == MULTIPART_NEED_MORE){
        return ROUTE_NEED_MORE_BODY;
    }
    if(uploadResult == MULTIPART_COMPLETE){
        response.action = ResponseAction(ACTION_UPLOAD_SUCCESS_JSON);
    }else if(uploadResult == MULTIPART_INVALID_FILENAME){
        response.action = ResponseAction(ACTION_UPLOAD_INVALID_JSON);
    }else{
        response.action = ResponseAction(ACTION_UPLOAD_FAILED_JSON);
    }

    request.status = HANDLE_COMPLETE;
    return ROUTE_HANDLED;
}

}

RouteResult routeRequest(Request &request, Response &response, int clientFd){
    if(request.requestMethod == "GET"){
        return routeGet(request, response);
    }
    if(request.requestMethod == "POST"){
        RouteResult formResult = routePostForm(request, response);
        if(formResult != ROUTE_NOT_HANDLED){
            return formResult;
        }

        RouteResult multipartResult = routePostMultipart(request, response, clientFd);
        if(multipartResult != ROUTE_NOT_HANDLED){
            return multipartResult;
        }

        response.action = ResponseAction(ACTION_BAD_REQUEST);
        request.status = HANDLE_COMPLETE;
        return ROUTE_HANDLED;
    }
    response.action = ResponseAction(ACTION_METHOD_NOT_ALLOWED);
    request.status = HANDLE_COMPLETE;
    return ROUTE_HANDLED;
}
