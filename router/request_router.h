/*  文件说明：
 *  1. 定义 HTTP 请求到业务动作 ResponseAction 的路由入口。
 *  2. RequestProcessor 在请求进入 HANDLE_BODY 后调用 routeRequest()。
 *  3. 路由层负责区分 GET、表单 POST、multipart 上传，并完成登录态校验和请求体解析。
 *  4. 路由层不拼 HTTP 状态行、响应头或 JSON，响应格式由 ActionResponseBuilder/ResponseBuilder 统一处理。
 */
#ifndef REQUEST_ROUTER_H
#define REQUEST_ROUTER_H

#include "../message/message.h"
#include "response_action.h"

enum RouteResult{
    ROUTE_NEED_MORE_BODY,  // 路由匹配成功，但请求体还没接收完整。
    ROUTE_HANDLED,         // 已经设置 response.action，后续进入响应构建。
    ROUTE_NOT_HANDLED      // 当前路由不处理该请求，调用方继续尝试其他流程。
};

// 根据请求方法、路径和登录态决定 ResponseAction。
// 路由层不直接拼响应体，只负责把业务意图传给响应构造层。
RouteResult routeRequest(Request &request, Response &response, int clientFd);

#endif
