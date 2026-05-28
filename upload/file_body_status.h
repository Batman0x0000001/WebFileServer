/*  文件说明：
 *  1. 定义 multipart 文件体解析过程中的细分状态。
 *  2. Request::fileMsgStatus 使用该枚举保存上传状态机进度。
 *  3. 状态会跨多次非阻塞 recv 保留，直到 boundary、part header 和文件内容全部处理完成。
 */
#ifndef FILE_BODY_STATUS_H
#define FILE_BODY_STATUS_H

enum FILEMSGBODYSTATUS{
    FILE_BEGIN_FLAG,   // 正在获取并处理表示文件开始的标志行
    FILE_HEAD,         // 正在获取并处理文件属性部分
    FILE_CONTENT,      // 正在获取并处理文件内容的部分
    FILE_COMPLETE      // 文件已经处理完成
};

#endif
