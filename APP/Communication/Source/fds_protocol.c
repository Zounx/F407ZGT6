/**
 * @file    fds_protocol.c
 * @brief   FDS 协议编解码层实现
 */
#include "fds_protocol.h"
#include <string.h>

/* ============================================================================
 * 编解码接口实现
 * ============================================================================ */

int ProtocolEncode(uint8_t *buf, int32_t function_id,
                   int32_t request_id,
                   const void *body, int32_t body_len)
{
    struct PacketHeader *hdr;
    int32_t total_len;

    if (buf == NULL) return -1;

    /* 限制 body 最小为 0 */
    if (body_len < 0) body_len = 0;

    total_len = PACKET_HEADER_SIZE + body_len;

    if (total_len > PACKET_MAX_SIZE) return -2;

    /* 填入头部 */
    hdr = (struct PacketHeader *)buf;
    hdr->packet_length = total_len;
    hdr->function_id   = function_id;
    hdr->request_id    = request_id;
    hdr->body_length   = body_len;

    /* 填入 body */
    if (body != NULL && body_len > 0) 
		{
        memcpy(buf + PACKET_HEADER_SIZE, body, body_len);
    }

    return total_len;
}

int ProtocolDecodeHeader(const uint8_t *buf, int32_t buf_len,
                         struct PacketHeader *hdr)
{
    if (buf == NULL || hdr == NULL) return -1;
    if (buf_len < PACKET_HEADER_SIZE) return -1;

    /* 直接内存拷贝解析头部（小端，与 STM32 一致） */
    memcpy(hdr, buf, sizeof(struct PacketHeader));

    /* 基本校验 */
    if (hdr->packet_length < PACKET_HEADER_SIZE) return -2;
    if (hdr->packet_length > PACKET_MAX_SIZE)    return -2;
    if (hdr->body_length < 0)                     return -2;
    if (hdr->packet_length != PACKET_HEADER_SIZE + hdr->body_length) return -2;

    return 0;
}

/* ============================================================================
 * 消息分发
 * ============================================================================ */

int ProtocolDispatch(const uint8_t *buf, int32_t len,
                     const struct PushEntry *table,
                     int table_size)
{
    struct PacketHeader hdr;
    int i;

    if (buf == NULL || table == NULL || table_size <= 0) return -1;

    /* 解析头部 */
    if (ProtocolDecodeHeader(buf, len, &hdr) != 0) return -2;

    /* 修正 body_length：头部声明的长度可能超出实际可用数据 */
    if (hdr.body_length > len - PACKET_HEADER_SIZE) 
		{
        hdr.body_length = len - PACKET_HEADER_SIZE;
        if (hdr.body_length < 0) hdr.body_length = 0;
    }

    /* 查表分发 */
    for (i = 0; i < table_size; i++) 
	 {
        if (table[i].function_id == hdr.function_id) 
				{
            const uint8_t *body = (hdr.body_length > 0)
                                  ? buf + PACKET_HEADER_SIZE : NULL;
            table[i].handler(&hdr, body);
            return 0;
        }
    }

    /* 未找到对应处理器 */
    return 1;
}
