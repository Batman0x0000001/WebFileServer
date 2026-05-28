#include "action_response_builder.h"

#include "response_builder.h"
#include "../fileservice/file_query_service.h"

namespace{

std::string jsonEscape(const std::string &value){
    std::string escaped;
    escaped.reserve(value.size());
    for(const auto &ch : value){
        switch(ch){
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default: {
            unsigned char uch = static_cast<unsigned char>(ch);
            if(uch < 0x20){
                escaped += "\\u00";
                const char *hex = "0123456789abcdef";
                escaped.push_back(hex[(uch >> 4) & 0x0f]);
                escaped.push_back(hex[uch & 0x0f]);
            }else{
                escaped.push_back(ch);
            }
            break;
        }
        }
    }
    return escaped;
}

std::string jsonMessage(int code, const std::string &message){
    return "{\"code\":" + std::to_string(code) + ",\"message\":\"" + jsonEscape(message) + "\"}";
}

void buildFileList(Response &response){
    FileListQueryResult result = loadUserFileList(response.username);
    if(result.status == FILE_QUERY_UNAUTHORIZED){
        ResponseBuilder::buildJsonResponse(response, HTTP_UNAUTHORIZED, jsonMessage(401, "please login first"));
        return;
    }
    if(result.status != FILE_QUERY_OK){
        ResponseBuilder::buildJsonResponse(response, HTTP_INTERNAL_ERROR, jsonMessage(500, "failed to load file list"));
        return;
    }

    std::string body = "{\"files\":[";
    for(size_t i = 0; i < result.metas.size(); ++i){
        if(i > 0){
            body += ",";
        }
        body += "{\"name\":\"" + jsonEscape(result.metas[i].filename) + "\",";
        body += "\"size\":" + std::to_string(result.metas[i].fileSize) + ",";
        body += "\"upload_time\":\"" + jsonEscape(result.metas[i].uploadTime) + "\",";
        body += "\"download_count\":" + std::to_string(result.metas[i].downloadCount) + "}";
    }
    body += "]}";
    ResponseBuilder::buildJsonResponse(response, HTTP_OK, body);
}

void buildDownload(Response &response){
    DownloadFileQueryResult result = openUserDownloadFile(response.username, response.action.value);
    if(result.status == FILE_QUERY_UNAUTHORIZED){
        ResponseBuilder::buildJsonResponse(response, HTTP_UNAUTHORIZED, jsonMessage(401, "please login first"));
        return;
    }
    if(result.status == FILE_QUERY_INVALID_FILENAME){
        ResponseBuilder::buildJsonResponse(response, HTTP_BAD_REQUEST, jsonMessage(400, "invalid filename"));
        return;
    }
    if(result.status == FILE_QUERY_NOT_FOUND){
        ResponseBuilder::buildJsonResponse(response, HTTP_NOT_FOUND, jsonMessage(404, "file not found"));
        return;
    }
    if(result.status != FILE_QUERY_OK){
        ResponseBuilder::buildJsonResponse(response, HTTP_INTERNAL_ERROR, jsonMessage(500, "failed to open file"));
        return;
    }

    response.fileMsgFd = result.fileFd;
    response.bodyFileName = result.filename;
    response.msgBodyLen = result.fileSize;
    ResponseBuilder::buildFileResponseHeader(response, response.msgBodyLen, result.filename);
}

void buildDelete(Response &response){
    DeleteFileQueryResult result = deleteUserFileMeta(response.username, response.action.value);
    if(result.status == FILE_QUERY_UNAUTHORIZED){
        ResponseBuilder::buildJsonResponse(response, HTTP_UNAUTHORIZED, jsonMessage(401, "please login first"));
        return;
    }
    if(result.status == FILE_QUERY_INVALID_FILENAME){
        ResponseBuilder::buildJsonResponse(response, HTTP_BAD_REQUEST, jsonMessage(400, "invalid filename"));
        return;
    }
    if(result.status != FILE_QUERY_OK){
        ResponseBuilder::buildJsonResponse(response, HTTP_INTERNAL_ERROR, jsonMessage(500, "delete failed"));
        return;
    }

    ResponseBuilder::buildJsonResponse(response, HTTP_OK, jsonMessage(200, "deleted"));
}

}

void ActionResponseBuilder::buildFromAction(Response &response){
    switch(response.action.type){
    case ACTION_FILE_LIST_JSON:
        buildFileList(response);
        break;
    case ACTION_DOWNLOAD:
        buildDownload(response);
        break;
    case ACTION_DELETE_JSON:
        buildDelete(response);
        break;
    case ACTION_UPLOAD_SUCCESS_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_OK, jsonMessage(200, "uploaded"));
        break;
    case ACTION_UPLOAD_INVALID_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_BAD_REQUEST, jsonMessage(400, "invalid filename"));
        break;
    case ACTION_UPLOAD_FAILED_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_INTERNAL_ERROR, jsonMessage(500, "upload failed"));
        break;
    case ACTION_UPLOAD_TOO_LARGE_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_PAYLOAD_TOO_LARGE, jsonMessage(413, "upload file is too large"));
        break;
    case ACTION_REGISTER_SUCCESS_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_OK, jsonMessage(200, "register success"));
        break;
    case ACTION_REGISTER_INVALID_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_BAD_REQUEST, jsonMessage(400, "invalid username or password"));
        break;
    case ACTION_REGISTER_DUPLICATE_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_CONFLICT, jsonMessage(409, "username already exists"));
        break;
    case ACTION_LOGIN_SUCCESS_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_OK, jsonMessage(200, "login success"),
                                           "Set-Cookie: token=" + response.action.value + "; Path=/; HttpOnly; SameSite=Lax\r\n");
        break;
    case ACTION_LOGIN_UNAUTHORIZED_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_UNAUTHORIZED, jsonMessage(401, "invalid username or password"));
        break;
    case ACTION_ME_SUCCESS_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_OK, "{\"code\":200,\"username\":\"" + jsonEscape(response.action.value) + "\"}");
        break;
    case ACTION_ME_UNAUTHORIZED_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_UNAUTHORIZED, jsonMessage(401, "please login first"));
        break;
    case ACTION_LOGOUT_SUCCESS_JSON:
        ResponseBuilder::buildJsonResponse(response, HTTP_OK, jsonMessage(200, "logout success"),
                                           "Set-Cookie: token=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax\r\n");
        break;
    case ACTION_BAD_REQUEST:
        ResponseBuilder::buildJsonResponse(response, HTTP_BAD_REQUEST, jsonMessage(400, "bad request"));
        break;
    case ACTION_METHOD_NOT_ALLOWED:
        ResponseBuilder::buildJsonResponse(response, HTTP_METHOD_NOT_ALLOWED, jsonMessage(405, "method not allowed"), "Allow: GET, POST\r\n");
        break;
    case ACTION_NOT_IMPLEMENTED:
        ResponseBuilder::buildJsonResponse(response, HTTP_NOT_IMPLEMENTED, jsonMessage(501, "not implemented"));
        break;
    case ACTION_NOT_FOUND:
    case ACTION_NONE:
    default:
        ResponseBuilder::buildJsonResponse(response, HTTP_NOT_FOUND, jsonMessage(404, "not found"));
        break;
    }
}
