/*  文件说明：
 *  1. 定义认证相关路由动作处理函数。
 *  2. request_router.cpp 将 /api/me、/api/logout、/api/register、/api/login 分发到这些函数。
 *  3. 函数只解析请求体、调用 auth.h 的认证能力并设置 ResponseAction，不直接构造 HTTP 响应。
 *  4. 请求体不完整时返回 ROUTE_NEED_MORE_BODY，由 RequestProcessor 继续接收。
 */
#ifndef AUTH_ACTIONS_H
#define AUTH_ACTIONS_H

#include "../router/request_router.h"

RouteResult handleMeAction(Request &request, Response &response);
// 删除 Redis session 并设置清 Cookie 的响应动作。
RouteResult handleLogoutAction(Request &request, Response &response);
// 解析注册表单、校验用户名密码并保存用户。
RouteResult handleRegisterAction(Request &request, Response &response);
// 解析登录表单、校验密码并创建 session token。
RouteResult handleLoginAction(Request &request, Response &response);

#endif
