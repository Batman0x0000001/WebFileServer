/*  文件说明：
 *  1. 定义路由层和响应构造层之间传递业务结果的轻量结构。
 *  2. request_router.cpp 只设置 ResponseAction，不直接生成 HTTP 响应内容。
 *  3. action_response_builder.cpp 根据 action type 和 value 构造 JSON、Cookie、文件下载或错误响应。
 *  4. value 字段用于携带 token、username、编码文件名等少量动作参数。
 */
#ifndef RESPONSE_ACTION_H
#define RESPONSE_ACTION_H

#include <string>

enum RESPONSEACTIONTYPE{
    ACTION_NONE,                    // 未设置业务动作。
    ACTION_FILE_LIST_JSON,          // 返回当前用户文件列表。
    ACTION_DOWNLOAD,                // 下载指定文件，value 保存 URL 编码文件名。
    ACTION_DELETE_JSON,             // 删除文件元信息，value 保存表单中的文件名。
    ACTION_UPLOAD_SUCCESS_JSON,     // 上传成功。
    ACTION_UPLOAD_INVALID_JSON,     // 上传文件名非法。
    ACTION_UPLOAD_FAILED_JSON,      // 上传解析或写文件失败。
    ACTION_UPLOAD_TOO_LARGE_JSON,   // 请求体超过 max_upload_size。
    ACTION_REGISTER_SUCCESS_JSON,   // 注册成功。
    ACTION_REGISTER_INVALID_JSON,   // 注册参数非法。
    ACTION_REGISTER_DUPLICATE_JSON, // 用户已存在或保存失败。
    ACTION_LOGIN_SUCCESS_JSON,      // 登录成功，value 保存 token。
    ACTION_LOGIN_UNAUTHORIZED_JSON, // 用户名或密码错误。
    ACTION_ME_SUCCESS_JSON,         // 当前登录用户查询成功，value 保存 username。
    ACTION_ME_UNAUTHORIZED_JSON,    // 未登录或 session 失效。
    ACTION_LOGOUT_SUCCESS_JSON,     // 退出登录成功。
    ACTION_BAD_REQUEST,             // 请求格式或业务参数错误。
    ACTION_METHOD_NOT_ALLOWED,      // HTTP 方法不支持。
    ACTION_NOT_IMPLEMENTED,         // 协议能力尚未实现，例如 chunked。
    ACTION_NOT_FOUND                // 路径或资源不存在。
};

struct ResponseAction{
    ResponseAction() : type(ACTION_NONE){
    }

    explicit ResponseAction(RESPONSEACTIONTYPE actionType) : type(actionType){
    }

    ResponseAction(RESPONSEACTIONTYPE actionType, const std::string &actionValue)
        : type(actionType), value(actionValue){
    }

    RESPONSEACTIONTYPE type;
    std::string value;
};

#endif
