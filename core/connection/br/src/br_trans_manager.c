/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2021. All rights reserved.
 * Description: br transmission management module.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "br_trans_manager.h"

#include <arpa/inet.h>

#include "securec.h"
#include "softbus_adapter_mem.h"
#include "softbus_conn_interface.h"
#include "softbus_conn_manager.h"
#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_json_utils.h"
#include "softbus_log.h"

static int32_t ReceivedHeadCheck(BrConnectionInfo *conn)
{
    int32_t pktHeadLen = sizeof(ConnPktHead);
    if (conn->recvPos < pktHeadLen) {
        return SOFTBUS_ERR;
    }
    ConnPktHead *head = (ConnPktHead *)(conn->recvBuf);
    if ((uint32_t)(head->magic) != MAGIC_NUMBER) {
        conn->recvPos = 0;
        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "[ReceivedHeadCheck] magic error 0x%x", head->magic);
        return SOFTBUS_ERR;
    }

    if (head->len + pktHeadLen > conn->recvSize) {
        conn->recvPos = 0;
        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR,
            "[ReceivedHeadCheck]data too large. module=%d, seq=%lld, datalen=%d", head->module, head->seq, head->len);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t BrTransReadOneFrame(uint32_t connectionId, const SppSocketDriver *sppDriver, int32_t clientId, char **outBuf)
{
    BrConnectionInfo *conn = GetConnectionRef(connectionId);
    if (conn == NULL) {
        return BR_READ_FAILED;
    }
    int32_t recvLen;
    int32_t bufLen;
    while (1) {
        if (conn->recvSize - conn->recvPos > 0) {
            recvLen = sppDriver->Read(clientId, conn->recvBuf + conn->recvPos, conn->recvSize - conn->recvPos);
            if (recvLen == BR_READ_SOCKET_CLOSED) {
                ReleaseConnectionRef(conn);
                return BR_READ_SOCKET_CLOSED;
            }
            if (recvLen == BR_READ_FAILED) {
                SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "sppDriver Read BR_READ_FAILED");
                continue;
            }
            conn->recvPos += recvLen;
        }
        if (ReceivedHeadCheck(conn) != SOFTBUS_OK) {
            continue;
        }
        bufLen = conn->recvPos;
        ConnPktHead *head = (ConnPktHead *)(conn->recvBuf);
        int32_t packLen = head->len + sizeof(ConnPktHead);
        if (bufLen < packLen) {
            SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_INFO, "not a complete package, continue");
            continue;
        }

        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_INFO, "[BrTransRead] a complete package packLen: %d", packLen);
        char *dataCopy = SoftBusMalloc(packLen);
        if (dataCopy == NULL || memcpy_s(dataCopy, packLen, conn->recvBuf, packLen) != EOK) {
            SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "[BrTransRead] memcpy_s failed");
            SoftBusFree(dataCopy);
            continue;
        }

        if (bufLen > packLen &&
            memmove_s(conn->recvBuf, conn->recvSize, conn->recvBuf + packLen, bufLen - packLen) != EOK) {
            SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "[BrTransRead] memmove_s failed");
            SoftBusFree(dataCopy);
            continue;
        }
        conn->recvPos = bufLen - packLen;
        *outBuf = dataCopy;
        ReleaseConnectionRef(conn);
        return packLen;
    }
}

int32_t BrTransSend(int32_t connId, const SppSocketDriver *sppDriver,
    int32_t brSendPeerLen, const char *data, uint32_t len)
{
    BrConnectionInfo *brConnInfo = GetConnectionRef(connId);
    if (brConnInfo == NULL) {
        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "[BrTransSend] connId: %d, not fount failed", connId);
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_INFO, "BrTransSend");
    int32_t socketFd = brConnInfo->socketFd;
    if (socketFd == -1) {
        ReleaseConnectionRef(brConnInfo);
        return SOFTBUS_ERR;
    }
    int32_t ret = SOFTBUS_OK;
    int32_t writeRet;
    int32_t tempLen = len;
    while (tempLen > 0) {
        (void)pthread_mutex_lock(&brConnInfo->lock);
        while (brConnInfo->conGestState == BT_RFCOM_CONGEST_ON &&
            brConnInfo->state == BR_CONNECTION_STATE_CONNECTED) {
            SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_INFO, "wait congest");
            pthread_cond_wait(&brConnInfo->congestCond, &brConnInfo->lock);
            SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_INFO, "free congest");
            break;
        }
        (void)pthread_mutex_unlock(&brConnInfo->lock);

        int32_t sendLenth = tempLen;
        if (sendLenth > brSendPeerLen) {
            sendLenth = brSendPeerLen;
        }
        writeRet = sppDriver->Write(socketFd, data, sendLenth);
        if (writeRet == -1) {
            ret = SOFTBUS_ERR;
            break;
        }
        data += sendLenth;
        tempLen -= sendLenth;
    }
    ReleaseConnectionRef(brConnInfo);
    return ret;
}

static char *BrAddNumToJson(int32_t requestOrResponse, int32_t delta, int32_t count)
{
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "Cannot create cJSON object");
        return NULL;
    }
    if (requestOrResponse == METHOD_NOTIFY_REQUEST) {
        if (!AddNumberToJsonObject(json, KEY_METHOD, METHOD_NOTIFY_REQUEST) ||
            !AddNumberToJsonObject(json, KEY_DELTA, delta) ||
            !AddNumberToJsonObject(json, KEY_REFERENCE_NUM, count)) {
            cJSON_Delete(json);
            return NULL;
        }
    } else {
        if (!AddNumberToJsonObject(json, KEY_METHOD, METHOD_NOTIFY_RESPONSE) ||
            !AddNumberToJsonObject(json, KEY_REFERENCE_NUM, count)) {
            cJSON_Delete(json);
            return NULL;
        }
    }
    char *data = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return data;
}

char *BrPackRequestOrResponse(int32_t requestOrResponse, int32_t delta, int32_t count, int32_t *outLen)
{
    char *data = BrAddNumToJson(requestOrResponse, delta, count);
    if (data == NULL) {
        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "BrAddNumToJson failed");
        return NULL;
    }

    int32_t headSize = sizeof(ConnPktHead);
    int32_t dataLen = strlen(data) + 1 + headSize;
    char *buf = (char *)SoftBusCalloc(dataLen);
    if (buf == NULL) {
        cJSON_free(data);
        return NULL;
    }
    ConnPktHead head;
    head.magic = MAGIC_NUMBER;
    head.module = MODULE_CONNECTION;
    head.seq = 1;
    head.flag = 0;
    head.len = strlen(data) + 1;

    if (memcpy_s(buf, dataLen, (void *)&head, headSize)) {
        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "memcpy_s head error");
        cJSON_free(data);
        SoftBusFree(buf);
        return NULL;
    }
    if (memcpy_s(buf + headSize, dataLen - headSize, data, strlen(data) + 1)) {
        SoftBusLog(SOFTBUS_LOG_CONN, SOFTBUS_LOG_ERROR, "memcpy_s data error");
        cJSON_free(data);
        SoftBusFree(buf);
        return NULL;
    }
    *outLen = dataLen;
    cJSON_free(data);
    return buf;
}
