/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "auth_socket.h"

#include <securec.h>

#include "auth_common.h"
#include "auth_connection.h"
#include "bus_center_manager.h"
#include "softbus_adapter_mem.h"
#include "softbus_base_listener.h"
#include "softbus_datahead_transform.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_socket.h"

#define AUTH_DEFAULT_PORT (-1)
#define AUTH_HEART_TIME (10 * 60)

static SoftbusBaseListener g_ethListener = {0};

int32_t AuthOpenTcpChannel(const ConnectOption *option, bool isNonBlock)
{
    char localIp[IP_LEN] = {0};
    if (LnnGetLocalStrInfo(STRING_KEY_WLAN_IP, localIp, sizeof(localIp)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth get local ip failed");
        return SOFTBUS_ERR;
    }

    int fd = ConnOpenClientSocket(option, localIp, isNonBlock);
    if (fd < 0) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth ConnOpenClientSocket failed");
        return SOFTBUS_ERR;
    }
    if (AddTrigger(AUTH, fd, isNonBlock ? WRITE_TRIGGER : READ_TRIGGER) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth AddTrigger failed");
        AuthCloseTcpFd(fd);
        return SOFTBUS_ERR;
    }
    if (ConnSetTcpKeepAlive(fd, AUTH_HEART_TIME) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth set tcp keep alive failed");
        AuthCloseTcpFd(fd);
        return SOFTBUS_ERR;
    }
    return fd;
}

int32_t HandleIpVerifyDevice(AuthManager *auth, const ConnectOption *option)
{
    if (auth == NULL || option == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "invalid parameter");
        return SOFTBUS_ERR;
    }
    int fd = AuthOpenTcpChannel(option, true);
    if (fd < 0) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth AuthOpenTcpChannel failed");
        return SOFTBUS_ERR;
    }
    auth->fd = fd;
    return SOFTBUS_OK;
}

static bool IsAuthTransModule(int32_t module)
{
    switch (module) {
        case MODULE_UDP_INFO:
        case MODULE_AUTH_CHANNEL:
        case MODULE_AUTH_MSG:
        case MODULE_P2P_LINK:
        case MODULE_P2P_LISTEN:
            return true;
        default:
            break;
    }
    return false;
}

static void AuthIpOnDataReceived(int32_t fd, const ConnPktHead *head, char *data, int len)
{
    (void)len;
    if (head == NULL || data == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "invalid parameter");
        return;
    }
    AuthManager *auth = NULL;
    auth = AuthGetManagerByFd(fd);
    if (auth == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "ip get auth failed");
        return;
    }
    if (!IsAuthTransModule(head->module)) {
        if (auth->authId != head->seq && auth->authId != fd &&
            (head->seq != 0 || head->module != MODULE_AUTH_CONNECTION)) {
            SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR,
                "handle verify device failed, authId = %" PRId64 ", fd = %d", auth->authId, fd);
            return;
        }
    }
    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "auth ip data module is %d len %d", head->module, len);
    AuthPrintDfxMsg((uint32_t)head->module, data, len);
    switch (head->module) {
        case MODULE_TRUST_ENGINE: {
            if (auth->side == SERVER_SIDE_FLAG && head->flag == 0 && auth->authId == fd) {
                auth->authId = head->seq;
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "server ip authId is %" PRId64, auth->authId);
            }
            HandleReceiveDeviceId(auth, (uint8_t *)data);
            break;
        }
        case MODULE_AUTH_SDK:
            HandleReceiveAuthData(auth, head->module, (uint8_t *)data, head->len);
            break;
        case MODULE_AUTH_CONNECTION:
            AuthHandlePeerSyncDeviceInfo(auth, (uint8_t *)data, head->len);
            break;
        case MODULE_UDP_INFO:
        case MODULE_P2P_LINK:
        case MODULE_P2P_LISTEN:
            AuthHandleTransInfo(auth, head, data);
            break;
        case MODULE_TIME_SYNC:
        case MODULE_AUTH_CHANNEL:
        case MODULE_AUTH_MSG: {
            if (auth->authId == 0) {
                auth->authId = GetSeq(SERVER_SIDE_FLAG);
            }
            AuthHandleTransInfo(auth, head, data);
            break;
        }
        default:
            SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "unknown data type");
            break;
    }
}

static void AuthNotifyDisconn(int32_t fd)
{
    AuthManager *auth = NULL;
    auth = AuthGetManagerByFd(fd);
    if (auth == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "ip get auth failed");
        return;
    }
    int64_t authId = auth->authId;
    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "auth disconnect");
    AuthNotifyTransDisconn(authId);
    AuthNotifyLnnDisconn(authId);
}

static void AuthIpDataProcess(int32_t fd, const ConnPktHead *head)
{
    char *data = NULL;
    uint32_t remainLen;
    ssize_t len;

    char *ipData = (char *)SoftBusMalloc(head->len);
    if (ipData == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "SoftBusMalloc failed");
        return;
    }
    data = ipData;
    remainLen = head->len;
    do {
        len = ConnRecvSocketData(fd, data, remainLen, 0);
        if (len <= 0) {
            SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth recv data len not correct, len %d", len);
            break;
        } else if ((uint32_t)len < remainLen) {
            data = data + len;
            remainLen = remainLen - (uint32_t)len;
        } else {
            AuthIpOnDataReceived(fd, head, ipData, head->len);
            remainLen = 0;
        }
    } while (remainLen > 0);
    SoftBusFree(ipData);
}

static int32_t TrySyncDeviceUuid(int32_t fd)
{
    AuthManager *auth = AuthGetManagerByFd(fd);
    if (auth == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "get auth failed in TrySyncDeviceUuid");
        return SOFTBUS_ERR;
    }
    if (auth->side != CLIENT_SIDE_FLAG || auth->status != WAIT_CONNECTION_ESTABLISHED) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "unexpected write event for auth: %" PRIu64 ", fd:%d.",
            auth->authId, fd);
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "connect successful for authId: %" PRIu64, auth->authId);
    (void)DelTrigger(AUTH, fd, WRITE_TRIGGER);
    if (AddTrigger(AUTH, fd, READ_TRIGGER) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth AddTrigger failed");
        AuthHandleFail(auth, SOFTBUS_CONN_FAIL);
        return SOFTBUS_ERR;
    }
    if (ConnToggleNonBlockMode(fd, false) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "set socket to block mode failed");
        AuthHandleFail(auth, SOFTBUS_CONN_FAIL);
        return SOFTBUS_ERR;
    }
    if (AuthSyncDeviceUuid(auth) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "AuthSyncDeviceUuid failed");
        AuthHandleFail(auth, SOFTBUS_AUTH_SYNC_DEVID_FAILED);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

static int32_t AuthOnDataEvent(ListenerModule module, int32_t events, int32_t fd)
{
    if (module != AUTH) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "%s:invalid module %d", __func__, module);
        return SOFTBUS_INVALID_PARAM;
    }
    if (events == SOFTBUS_SOCKET_OUT) {
        return TrySyncDeviceUuid(fd);
    } else if (events != SOFTBUS_SOCKET_IN) {
        return SOFTBUS_ERR;
    }
    uint32_t headSize = sizeof(ConnPktHead);
    ssize_t len;

    ConnPktHead head = {0};
    len = ConnRecvSocketData(fd, (void *)&head, headSize, 0);
    UnpackConnPktHead(&head);
    if (len < (int32_t)headSize) {
        if (len < 0) {
            SoftBusLog(
                SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth ConnRecvSocketData failed, DelTrigger. recv len=%zd", len);
            (void)DelTrigger(AUTH, fd, READ_TRIGGER);
            AuthNotifyDisconn(fd);
        }
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth recv data head len not correct, len is %d", len);
        return SOFTBUS_ERR;
    }

    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO,
        "auth recv eth data, head len is %d, module = %d, flag = %d, seq = %" PRId64,
        head.len, head.module, head.flag, head.seq);
    if (head.len > AUTH_MAX_DATA_LEN) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth head len is out of size");
        return SOFTBUS_ERR;
    }
    if ((uint32_t)head.magic != MAGIC_NUMBER) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth recv invalid packet head");
        return SOFTBUS_ERR;
    }
    AuthIpDataProcess(fd, &head);

    return SOFTBUS_OK;
}

int32_t AuthSocketSendData(AuthManager *auth, const AuthDataHead *head, const uint8_t *data, uint32_t len)
{
    if (auth == NULL || head == NULL || data == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "invalid parameter");
        return SOFTBUS_ERR;
    }
    ConnPktHead ethHead;
    uint32_t postDataLen;
    char *connPostData = NULL;
    ethHead.magic = MAGIC_NUMBER;
    ethHead.module = head->module;
    ethHead.flag = head->flag;
    if (IsAuthTransModule(head->module)) {
        ethHead.seq = head->seq;
    } else if (head->module == MODULE_AUTH_CONNECTION && auth->side == SERVER_SIDE_FLAG) {
        ethHead.seq = 0;
    } else {
        ethHead.seq = auth->authId;
    }
    ethHead.len = len;
    postDataLen = sizeof(ConnPktHead) + len;
    char *buf = (char *)SoftBusMalloc(postDataLen);
    if (buf == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "SoftBusMalloc failed");
        return SOFTBUS_ERR;
    }
    connPostData = buf;
    PackConnPktHead(&ethHead);
    if (memcpy_s(buf, sizeof(ConnPktHead), &ethHead, sizeof(ConnPktHead)) != EOK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "memcpy_s failed");
        SoftBusFree(connPostData);
        return SOFTBUS_ERR;
    }
    buf += sizeof(ConnPktHead);
    if (memcpy_s(buf, len, data, len) != EOK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "memcpy_s failed");
        SoftBusFree(connPostData);
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO,
        "auth start post eth data, authId is %" PRId64 ", fd is %d, moduleId is %d, len is %u",
        auth->authId, auth->fd, head->module, len);
    ssize_t byte = ConnSendSocketData(auth->fd, connPostData, postDataLen, 0);
    if (byte != (ssize_t)postDataLen) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "ConnSendSocketData failed");
        SoftBusFree(connPostData);
        return SOFTBUS_ERR;
    }
    SoftBusFree(connPostData);
    return SOFTBUS_OK;
}

static int32_t AuthOnConnectEvent(ListenerModule module, int32_t events, int32_t cfd, const ConnectOption *clientAddr)
{
    if (module != AUTH) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "%s:invalid module %d", __func__, module);
        return SOFTBUS_INVALID_PARAM;
    }
    if (events == SOFTBUS_SOCKET_EXCEPTION) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth Exception occurred");
        return SOFTBUS_ERR;
    }
    if (cfd < 0 || clientAddr == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "%s:invalid parameter", __func__);
        return SOFTBUS_INVALID_PARAM;
    }

    if (clientAddr->socketOption.port <= 0) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth GetTcpSockPort failed");
        return SOFTBUS_ERR;
    }
    if (AddTrigger(AUTH, cfd, READ_TRIGGER) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth AddTrigger failed");
        return SOFTBUS_ERR;
    }
    if (AddAuthServer(cfd, clientAddr) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth AddAuthServer failed");
        AuthCloseTcpFd(cfd);
        return SOFTBUS_ERR;
    }
    if (ConnSetTcpKeepAlive(cfd, AUTH_HEART_TIME) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth server set tcp keep alive failed");
        AuthCloseTcpFd(cfd);
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t OpenAuthServer(void)
{
    int32_t localPort;
    g_ethListener.onConnectEvent = AuthOnConnectEvent;
    g_ethListener.onDataEvent = AuthOnDataEvent;
    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "auth open base listener");
    if (SetSoftbusBaseListener(AUTH, &g_ethListener) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth SetSoftbusBaseListener failed");
        return AUTH_ERROR_CODE;
    }

    LocalListenerInfo info = {
        .type = CONNECT_TCP,
        .socketOption = {
            .addr = {0},
            .moduleId = AUTH,
            .port = 0,
            .protocol = LNN_PROTOCOL_IP
        }
    };

    if (LnnGetLocalStrInfo(STRING_KEY_WLAN_IP, info.socketOption.addr, sizeof(info.socketOption.addr)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth LnnGetLocalStrInfo failed");
        return AUTH_ERROR_CODE;
    }

    localPort = StartBaseListener(&info);
    if (localPort <= 0) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth StartBaseListener failed!");
        return AUTH_ERROR_CODE;
    }
    return localPort;
}

void AuthCloseTcpFd(int32_t fd)
{
    (void)DelTrigger(AUTH, fd, RW_TRIGGER);
    ConnShutdownSocket(fd);
}

void CloseAuthServer(void)
{
    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "close auth listener");
    if (StopBaseListener(AUTH) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "auth StopBaseListener failed");
    }
}
