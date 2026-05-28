/*  文件说明：
 *  1. 定义 ResponseAction 到具体 HTTP 响应的业务构造入口。
 *  2. ResponseSender 在 Response 仍处于 HANDLE_INIT 时调用 buildFromAction()。
 *  3. 本类负责文件列表、下载、删除、登录注册等业务响应分支。
 *  4. 底层状态行、响应头和响应体字符串拼装委托 ResponseBuilder 完成。
 */
#ifndef ACTION_RESPONSE_BUILDER_H
#define ACTION_RESPONSE_BUILDER_H

#include "../message/message.h"

class ActionResponseBuilder{
public:
    // 根据 response.action 填充 Response 的状态码、响应头、响应体或文件 fd。
    static void buildFromAction(Response &response);
};

#endif
