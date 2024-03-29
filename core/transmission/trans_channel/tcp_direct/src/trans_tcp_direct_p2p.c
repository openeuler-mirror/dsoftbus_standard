/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "trans_tcp_direct_p2p.h"

#include <securec.h>

#include "auth_interface.h"
#include "cJSON.h"
#include "p2plink_interface.h"
#include "softbus_adapter_mem.h"
#include "softbus_base_listener.h"
#include "softbus_def.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_socket.h"
#include "trans_tcp_direct_json.h"
#include "trans_tcp_direct_listener.h"
#include "trans_tcp_direct_message.h"
#include "trans_tcp_direct_sessionconn.h"

static int32_t g_p2pSessionPort = -1;
static char g_p2pSessionIp[IP_LEN] = {0};

static int32_t StartNewP2pListener(const char *ip, int32_t *port)
{
    int32_t listenerPort;

    LocalListenerInfo info = {
        .type = CONNECT_P2P,
        .socketOption = {
            .addr = "",
            .port = *port,
            .protocol = LNN_PROTOCOL_IP,
            .moduleId = DIRECT_CHANNEL_SERVER_P2P
        }
    };
    if (strcpy_s(info.socketOption.addr, sizeof(info.socketOption.addr), ip) != EOK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "%s:copy addr failed!", __func__);
        return SOFTBUS_ERR;
    }

    listenerPort = TransTdcStartSessionListener(DIRECT_CHANNEL_SERVER_P2P, &info);
    if (listenerPort < 0) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "StartNewP2pListener start listener fail");
        return SOFTBUS_ERR;
    }
    *port = listenerPort;
    return SOFTBUS_OK;
}

void StopP2pSessionListener(void)
{
    if (g_p2pSessionPort > 0) {
        if (StopBaseListener(DIRECT_CHANNEL_SERVER_P2P) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "StopP2pSessionListener stop listener fail");
        }
    }

    g_p2pSessionPort = -1;
    if (strcpy_s(g_p2pSessionIp, IP_LEN, "") != EOK) {
    }
    return;
}

static void NotifyP2pSessionConnClear(ListNode *sessionConnList)
{
    if (sessionConnList == NULL) {
        return;
    }

    SessionConn *item = NULL;
    SessionConn *nextItem = NULL;
    
    LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, sessionConnList, SessionConn, node) {
        (void)NotifyChannelOpenFailed(item->channelId);
        TransSrvDelDataBufNode(item->channelId);

        SoftBusFree(item);
    }
}

static void ClearP2pSessionConn(void)
{
    SessionConn *item = NULL;
    SessionConn *nextItem = NULL;
    
    SoftBusList *sessionList = GetSessionConnList();
    if (sessionList == NULL) {
        return;
    }
    if (GetSessionConnLock() != SOFTBUS_OK) {
        return;
    }
    
    ListNode tempSessionConnList;
    ListInit(&tempSessionConnList);
    LIST_FOR_EACH_ENTRY_SAFE(item, nextItem, &sessionList->list, SessionConn, node) {
        if (item->status < TCP_DIRECT_CHANNEL_STATUS_CONNECTED && item->appInfo.routeType == WIFI_P2P) {
            ListDelete(&item->node);
            sessionList->cnt--;
        
            ListAdd(&tempSessionConnList, &item->node);
        }
    }
    ReleaseSessonConnLock();

    NotifyP2pSessionConnClear(&tempSessionConnList);
}

static int32_t StartP2pListener(const char *ip, int32_t *port)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "StartP2pListener: port=%d", *port);
    if (g_p2pSessionPort > 0 && strcmp(ip, g_p2pSessionIp) != 0) {
        ClearP2pSessionConn();
        StopP2pSessionListener();
    }
    if (g_p2pSessionPort > 0) {
        *port = g_p2pSessionPort;
        return SOFTBUS_OK;
    }

    if (StartNewP2pListener(ip, port) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "StartP2pListener start new listener fail");
        return SOFTBUS_ERR;
    }

    g_p2pSessionPort = *port;
    if (strcpy_s(g_p2pSessionIp, sizeof(g_p2pSessionIp), ip) != EOK) {
        StopP2pSessionListener();
        return SOFTBUS_MEM_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "StartP2pListener end: port=%d", *port);
    return SOFTBUS_OK;
}

static void OnChannelOpenFail(int32_t channelId)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnChannelOpenFail: channelId=%d", channelId);
    NotifyChannelOpenFailed(channelId);
    TransDelSessionConnById(channelId);
    TransSrvDelDataBufNode(channelId);
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnChannelOpenFail end");
}

static char *EncryptVerifyP2pData(int64_t authId, const char *data, uint32_t *encryptDataLen)
{
    char *encryptData = NULL;
    uint32_t len;
    OutBuf buf = {0};
    AuthSideFlag side;

    len = strlen(data) + 1 + AuthGetEncryptHeadLen();
    encryptData = (char *)SoftBusCalloc(len);
    if (encryptData == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "malloc fail");
        return NULL;
    }

    buf.buf = (unsigned char *)encryptData;
    buf.bufLen = len;
    if (AuthEncryptBySeq((int32_t)authId, &side, (unsigned char *)data, strlen(data) + 1, &buf) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "EncryptVerifyP2pData encrypt fail");
        SoftBusFree(encryptData);
        return NULL;
    }
    if (buf.outLen != len) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "outLen not right");
        SoftBusFree(encryptData);
        return NULL;
    }

    *encryptDataLen = len;
    return encryptData;
}

static int32_t SendAuthData(int64_t authId, int32_t module, int32_t flag, int64_t seq, const char *data)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "SendAuthData: [%" PRId64 ", %d, %d, %" PRId64 "]",
        authId, module, flag, seq);
    uint32_t encryptDataLen;
    char *encryptData = NULL;
    int32_t ret;

    encryptData = EncryptVerifyP2pData(authId, data, &encryptDataLen);
    if (encryptData == NULL || encryptDataLen == 0) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "SendAuthData encrypt fail");
        return SOFTBUS_ENCRYPT_ERR;
    }

    AuthDataHead head = {0};
    head.dataType = DATA_TYPE_CONNECTION;
    head.module = module;
    head.authId = authId;
    head.flag = flag;
    head.seq = seq;
    ret = AuthPostData(&head, (unsigned char *)encryptData, encryptDataLen);
    SoftBusFree(encryptData);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "SendAuthData fail: ret=%d", ret);
        return ret;
    }
    return SOFTBUS_OK;
}

static int32_t VerifyP2p(int64_t authId, const char *myIp, int32_t myPort, int64_t seq)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "VerifyP2p: authId=%" PRId64 ", ip, port=%d",
        authId, myPort);
    char *msg = NULL;
    int32_t ret;

    msg = VerifyP2pPack(myIp, myPort);
    if (msg == NULL) {
        return SOFTBUS_PARSE_JSON_ERR;
    }
    ret = SendAuthData(authId, MODULE_P2P_LISTEN, MSG_FLAG_REQUEST, (int64_t)seq, msg);
    cJSON_free(msg);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "VerifyP2p send auth data fail");
        return ret;
    }
    return SOFTBUS_OK;
}

static void OnAuthConnOpened(uint32_t requestId, int64_t authId)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnAuthConnOpened: requestId=%u, authId=%" PRId64,
        requestId, authId);
    int32_t channelId = INVALID_CHANNEL_ID;
    SessionConn *conn = NULL;

    if (GetSessionConnLock() != SOFTBUS_OK) {
        goto EXIT_ERR;
    }
    conn = GetSessionConnByRequestId(requestId);
    if (conn == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnAuthConnOpened not find session");
        ReleaseSessonConnLock();
        goto EXIT_ERR;
    }
    channelId = conn->channelId;
    conn->authId = authId;
    conn->status = TCP_DIRECT_CHANNEL_STATUS_VERIFY_P2P;
    ReleaseSessonConnLock();

    if (VerifyP2p(authId, conn->appInfo.myData.addr, conn->appInfo.myData.port, conn->req) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnAuthConnOpened verify p2p fail");
        goto EXIT_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnAuthConnOpened end");
    return;
EXIT_ERR:
    if (channelId != INVALID_CHANNEL_ID) {
        OnChannelOpenFail(channelId);
    }
}

static void OnAuthConnOpenFailed(uint32_t requestId, int32_t reason)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnAuthConnOpenFailed: reqId=%u, reason=%d", requestId, reason);
    SessionConn *conn = NULL;
    int32_t channelId;

    if (GetSessionConnLock() != SOFTBUS_OK) {
        return;
    }
    conn = GetSessionConnByRequestId(requestId);
    if (conn == NULL) {
        ReleaseSessonConnLock();
        return;
    }
    channelId = conn->channelId;
    ReleaseSessonConnLock();

    OnChannelOpenFail(channelId);
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnAuthConnOpenFailed end");
    return;
}

static int32_t OpenAuthConn(const char *uuid, uint32_t reqId)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OpenAuthConn: requestId=%u", reqId);
    AuthConnInfo auth = {0};
    AuthConnCallback cb = {0};

    if (AuthGetPreferConnInfo(uuid, &auth) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OpenAuthConn get auth info fail");
        return SOFTBUS_ERR;
    }
    cb.onConnOpened = OnAuthConnOpened;
    cb.onConnOpenFailed = OnAuthConnOpenFailed;
    if (AuthOpenConn(&auth, reqId, &cb) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OpenAuthConn open auth conn fail");
        return SOFTBUS_TRANS_OPEN_AUTH_CONN_FAILED;
    }

    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OpenAuthConn end");
    return SOFTBUS_OK;
}

static char *DecryptVerifyP2pData(int64_t authId, const ConnectOption *option,
    const AuthTransDataInfo *info)
{
    if (info->len <= AuthGetEncryptHeadLen()) {
        return NULL;
    }
    int32_t ret;
    uint32_t len;
    char *data = NULL;
    OutBuf buf = {0};

    len = info->len - AuthGetEncryptHeadLen() + 1;
    data = (char *)SoftBusCalloc(len);
    if (data == NULL) {
        return NULL;
    }

    buf.buf = (unsigned char *)data;
    buf.bufLen = info->len - AuthGetEncryptHeadLen();
    ret = AuthDecrypt(option, AUTH_SIDE_ANY, (unsigned char *)info->data, info->len, &buf);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "DecryptVerifyP2pData decrypt fail: ret=%d", ret);
        SoftBusFree(data);
        return NULL;
    }

    return data;
}

static void SendVerifyP2pFailRsp(int64_t authId, int64_t seq,
    int32_t code, int32_t errCode, const char *errDesc)
{
    char *reply = VerifyP2pPackError(code, errCode, errDesc);
    if (reply == NULL) {
        return;
    }
    if (SendAuthData(authId, MODULE_P2P_LISTEN, MES_FLAG_REPLY, seq, reply) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "SendVerifyP2pFailRsp send auth data fail");
    }
    cJSON_free(reply);
    return;
}

static int32_t OnVerifyP2pRequest(int64_t authId, int64_t seq, const cJSON *json)
{
    SoftBusLog(
        SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnVerifyP2pRequest: authId=%" PRId64 ", seq=%" PRId64, authId, seq);
    int32_t peerPort;
    char peerIp[IP_LEN] = {0};
    int32_t myPort = 0;
    char myIp[IP_LEN] = {0};
    int32_t ret;

    ret = VerifyP2pUnPack(json, peerIp, IP_LEN, &peerPort);
    if (ret != SOFTBUS_OK) {
        SendVerifyP2pFailRsp(authId, seq, CODE_VERIFY_P2P, ret, "OnVerifyP2pRequest unpack fail");
        return ret;
    }

    if (P2pLinkGetLocalIp(myIp, sizeof(myIp)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnVerifyP2pRequest get p2p ip fail");
        SendVerifyP2pFailRsp(authId, seq, CODE_VERIFY_P2P, ret, "get p2p ip fail");
        return SOFTBUS_TRANS_GET_P2P_INFO_FAILED;
    }

    ret = StartP2pListener(myIp, &myPort);
    if (ret != SOFTBUS_OK) {
        SendVerifyP2pFailRsp(authId, seq, CODE_VERIFY_P2P, ret, "invalid p2p port");
        return SOFTBUS_ERR;
    }

    char *reply = VerifyP2pPack(myIp, myPort);
    if (reply == NULL) {
        SendVerifyP2pFailRsp(authId, seq, CODE_VERIFY_P2P, ret, "pack reply failed");
        return SOFTBUS_PARSE_JSON_ERR;
    }

    ret = SendAuthData(authId, MODULE_P2P_LISTEN, MES_FLAG_REPLY, seq, reply);
    cJSON_free(reply);
    if (ret != SOFTBUS_OK) {
        return ret;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnVerifyP2pRequest end");
    return SOFTBUS_OK;
}

static int32_t ConnectTcpDirectPeer(const char *addr, int port)
{
    ConnectOption options = {
        .type = CONNECT_P2P,
        .socketOption = {
            .addr = {0},
            .port = port,
            .protocol = LNN_PROTOCOL_IP,
            .moduleId = DIRECT_CHANNEL_CLIENT
        }
    };

    int32_t ret = strcpy_s(options.socketOption.addr, sizeof(options.socketOption.addr), addr);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "%s: strcpy_s failed!ret = %" PRId32, __func__, ret);
        return -1;
    }

    return ConnOpenClientSocket(&options, BIND_ADDR_ALL, true);
}

static int32_t OnVerifyP2pReply(int64_t authId, int64_t seq, const cJSON *json)
{
    SoftBusLog(
        SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnVerifyP2pReply: authId=%" PRId64 ", seq=%" PRId64, authId, seq);
    SessionConn *conn = NULL;
    int32_t ret;
    int32_t channelId = INVALID_CHANNEL_ID;
    int32_t fd;

    if (GetSessionConnLock() != SOFTBUS_OK) {
        return SOFTBUS_LOCK_ERR;
    }
    conn = GetSessionConnByReq(seq);
    if (conn == NULL) {
        ReleaseSessonConnLock();
        return SOFTBUS_NOT_FIND;
    }
    channelId = conn->channelId;

    ret = VerifyP2pUnPack(json, conn->appInfo.peerData.addr, IP_LEN, &conn->appInfo.peerData.port);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnVerifyP2pReply unpack fail: ret=%d", ret);
        ReleaseSessonConnLock();
        goto EXIT_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnVerifyP2pReply peer wifi: ip, port=%d",
        conn->appInfo.peerData.port);

    fd = ConnectTcpDirectPeer(conn->appInfo.peerData.addr, conn->appInfo.peerData.port);
    if (fd <= 0) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnVerifyP2pReply conn fail: fd=%d", fd);
        ReleaseSessonConnLock();
        goto EXIT_ERR;
    }
    conn->appInfo.fd = fd;
    conn->status = TCP_DIRECT_CHANNEL_STATUS_CONNECTING;
    ReleaseSessonConnLock();

    if (TransSrvAddDataBufNode(channelId, fd) != SOFTBUS_OK) {
        goto EXIT_ERR;
    }
    if (AddTrigger(DIRECT_CHANNEL_SERVER_P2P, fd, WRITE_TRIGGER) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnVerifyP2pReply add trigger fail");
        goto EXIT_ERR;
    }

    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnVerifyP2pReply end: fd=%d", fd);
    return SOFTBUS_OK;
EXIT_ERR:
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnVerifyP2pReply fail");
    if (channelId != INVALID_CHANNEL_ID) {
        OnChannelOpenFail(channelId);
    }
    return SOFTBUS_ERR;
}

static void OnAuthMsgProc(int64_t authId, int32_t flags, int64_t seq, const cJSON *json)
{
    int32_t ret;
    if (flags == MSG_FLAG_REQUEST) {
        ret = OnVerifyP2pRequest(authId, seq, json);
    } else {
        ret = OnVerifyP2pReply(authId, seq, json);
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnAuthMsgProc result: ret=%d", ret);
    return;
}

static void OnAuthDataRecv(int64_t authId, const ConnectOption *option, const AuthTransDataInfo *info)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnAuthDataRecv: authId=%" PRId64, authId);
    if (option == NULL || info == NULL || info->data == NULL) {
        return;
    }
    if (info->module != MODULE_P2P_LISTEN) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OnAuthDataRecv: info->module=%d", info->module);
        return;
    }

    char *data = DecryptVerifyP2pData(authId, option, info);
    if (data == NULL) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnAuthConnOpened decrypt fail");
        return;
    }
    AnonyPacketPrintout(SOFTBUS_LOG_TRAN, "OnAuthDataRecv data: ", data, strlen(data));

    cJSON *json = cJSON_Parse(data);
    SoftBusFree(data);
    if (json == NULL) {
        return;
    }

    OnAuthMsgProc(authId, info->flags, info->seq, json);
    cJSON_Delete(json);
    return;
}

static void OnAuthChannelClose(int64_t authId)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OnAuthChannelClose: authId=%" PRId64, authId);
    return;
}

int32_t OpenP2pDirectChannel(const AppInfo *appInfo, const ConnectOption *connInfo, int32_t *channelId)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OpenP2pDirectChannel");
    if (appInfo == NULL || connInfo == NULL || channelId == NULL || connInfo->type != CONNECT_P2P) {
        return SOFTBUS_INVALID_PARAM;
    }
    SessionConn *conn = NULL;
    int32_t newChannelId;
    int32_t ret;
    uint32_t requestId;

    conn = CreateNewSessinConn(DIRECT_CHANNEL_SERVER_P2P, false);
    if (conn == NULL) {
        return SOFTBUS_MEM_ERR;
    }
    newChannelId = conn->channelId;
    (void)memcpy_s(&conn->appInfo, sizeof(AppInfo), appInfo, sizeof(AppInfo));

    ret = StartP2pListener(conn->appInfo.myData.addr, &conn->appInfo.myData.port);
    if (ret != SOFTBUS_OK) {
        SoftBusFree(conn);
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OpenP2pDirectChannel start listener fail");
        return ret;
    }

    requestId = AuthGenRequestId();
    conn->status = TCP_DIRECT_CHANNEL_STATUS_AUTH_CHANNEL;
    conn->requestId = requestId;
    uint64_t seq = TransTdcGetNewSeqId();
    if (seq == INVALID_SEQ_ID) {
        SoftBusFree(conn);
        return SOFTBUS_ERR;
    }
    
    conn->req = (int64_t)seq;
    ret = TransTdcAddSessionConn(conn);
    if (ret != SOFTBUS_OK) {
        SoftBusFree(conn);
        return ret;
    }

    ret = OpenAuthConn(appInfo->peerData.deviceId, requestId);
    if (ret != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "OpenP2pDirectChannel open auth conn fail");
        TransDelSessionConnById(newChannelId);
        return ret;
    }

    *channelId = newChannelId;
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "OpenP2pDirectChannel end: channelId=%d", newChannelId);
    return SOFTBUS_OK;
}

int32_t P2pDirectChannelInit(void)
{
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "P2pDirectChannelInit");
    AuthTransCallback cb;
    cb.onTransUdpDataRecv = OnAuthDataRecv;
    cb.onAuthChannelClose = OnAuthChannelClose;

    if (AuthTransDataRegCallback(TRANS_P2P_LISTEN, &cb) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_ERROR, "P2pDirectChannelInit set cb fail");
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_TRAN, SOFTBUS_LOG_INFO, "P2pDirectChannelInit ok");
    return SOFTBUS_OK;
}

