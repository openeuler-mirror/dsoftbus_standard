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

#include "auth_common.h"

#include <securec.h>
#include <sys/time.h>

#include "bus_center_manager.h"
#include "softbus_adapter_mem.h"
#include "softbus_base_listener.h"
#include "softbus_bus_center.h"
#include "softbus_errcode.h"
#include "softbus_feature_config.h"
#include "softbus_log.h"
#include "softbus_utils.h"

#define DEFAULT_AUTH_ABILITY_COLLECTION 0
#define AUTH_SUPPORT_SERVER_SIDE_MASK 0x01
#define INTERVAL_VALUE 2
#define OFFSET_BITS 24
#define INT_MAX_VALUE 0xFFFFFEL
#define LOW_24_BITS 0xFFFFFFL
#define MAX_BYTE_RECORD 230
#define ANONYMOUS_INTEVER_LEN 60
#define ANONYMOUS_CHAR '*'

static uint64_t g_uniqueId = 0;
static uint32_t g_authAbility = 0;

int64_t GetSeq(AuthSideFlag flag)
{
    static uint64_t integer = 0;
    if (integer == INT_MAX_VALUE) {
        integer = 0;
    }
    integer += INTERVAL_VALUE;
    uint64_t temp = integer;
    if (flag == SERVER_SIDE_FLAG) {
        temp += 1;
    }
    temp = ((g_uniqueId << OFFSET_BITS) | (temp & LOW_24_BITS));
    int64_t seq = 0;
    if (memcpy_s(&seq, sizeof(int64_t), &temp, sizeof(uint64_t)) != EOK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "memcpy_s seq error");
    }
    return seq;
}

uint16_t AuthGetNextConnectionId(void)
{
    static uint16_t authConnId = 0;
    return ++authConnId;
}

AuthSideFlag AuthGetSideByRemoteSeq(int64_t seq)
{
    /* even odd check */
    return (seq % 2) == 0 ? SERVER_SIDE_FLAG : CLIENT_SIDE_FLAG;
}

void AuthGetAbility(void)
{
    if (SoftbusGetConfig(SOFTBUS_INT_AUTH_ABILITY_COLLECTION,
        (unsigned char*)&g_authAbility, sizeof(g_authAbility)) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "Cannot get auth ability from config file");
        g_authAbility = DEFAULT_AUTH_ABILITY_COLLECTION;
    }
    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "auth ability is %u", g_authAbility);
}

bool AuthIsSupportServerSide(void)
{
    return (g_authAbility & AUTH_SUPPORT_SERVER_SIDE_MASK) ? true : false;
}

void UniqueIdInit(void)
{
    struct timeval time = {0};
    gettimeofday(&time, NULL);
    g_uniqueId = (uint64_t)(time.tv_usec);
}

static int32_t GetRemoteIpByNodeAddr(char *ip, uint32_t size, const char *addr)
{
    NodeBasicInfo *info = NULL;
    int32_t num = 0;

    if (LnnGetAllOnlineNodeInfo(&info, &num) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "get online node fail");
        return SOFTBUS_ERR;
    }
    if (info == NULL) {
        SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "no online node");
        return SOFTBUS_NOT_FIND;
    }
    if (num == 0) {
        SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "num is 0");
        SoftBusFree(info);
        return SOFTBUS_NOT_FIND;
    }
    for (int32_t i = 0; i < num; i++) {
        char *tmpNetworkId = info[i].networkId;
        char nodeAddr[SHORT_ADDRESS_MAX_LEN] = {0};
        if (LnnGetRemoteStrInfo(tmpNetworkId, STRING_KEY_NODE_ADDR, nodeAddr, sizeof(nodeAddr)) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "%s: get node addr failed!", __func__);
            continue;
        }
        if (strcmp(nodeAddr, addr) == 0) {
            if (LnnGetRemoteStrInfo(tmpNetworkId, STRING_KEY_WLAN_IP, ip, size) == SOFTBUS_OK) {
                SoftBusFree(info);
                return SOFTBUS_OK;
            } else {
                SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "%s: get ip failed!", __func__);
                break;
            }
        }
    }

    SoftBusFree(info);
    SoftBusLog(SOFTBUS_LOG_COMM, SOFTBUS_LOG_INFO, "%s: no find", __func__);
    return SOFTBUS_NOT_FIND;
}

int32_t AuthGetDeviceKey(char *key, uint32_t size, uint32_t *len, const ConnectOption *option)
{
    if (key == NULL || len == NULL || option == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "invalid parameter");
        return SOFTBUS_ERR;
    }
    switch (option->type) {
        case CONNECT_BR:
            if (strcpy_s(key, size, option->brOption.brMac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "strcpy_s failed");
                return SOFTBUS_ERR;
            }
            *len = BT_MAC_LEN;
            break;
        case CONNECT_BLE:
            if (strcpy_s(key, size, option->bleOption.bleMac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "strcpy_s failed");
                return SOFTBUS_ERR;
            }
            *len = BT_MAC_LEN;
            break;
        case CONNECT_TCP:
            if (option->socketOption.protocol == LNN_PROTOCOL_IP) {
                if (strcpy_s(key, size, option->socketOption.addr) != EOK) {
                    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "strcpy_s failed");
                    return SOFTBUS_ERR;
                }
            } else {
                if (GetRemoteIpByNodeAddr(key, size, option->socketOption.addr) != SOFTBUS_OK) {
                    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "GetRemoteIpByNodeAddr failed");
                    return SOFTBUS_ERR;
                }
            }
            *len = IP_LEN;
            break;
        default:
            SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "unknown type");
            return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

int32_t AuthConvertConnInfo(ConnectOption *option, const ConnectionInfo *connInfo)
{
    if (option == NULL || connInfo == NULL) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "invalid parameter");
        return SOFTBUS_ERR;
    }
    option->type = connInfo->type;
    switch (connInfo->type) {
        case CONNECT_BR: {
            if (strcpy_s(option->brOption.brMac, BT_MAC_LEN, connInfo->brInfo.brMac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "strcpy_s failed");
                return SOFTBUS_ERR;
            }
            break;
        }
        case CONNECT_BLE:
            if (strcpy_s(option->bleOption.bleMac, BT_MAC_LEN, connInfo->bleInfo.bleMac) != EOK ||
                memcpy_s(option->bleOption.deviceIdHash, UDID_HASH_LEN,
                connInfo->bleInfo.deviceIdHash, UDID_HASH_LEN) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy bleMac or deviceIdHash failed");
                return SOFTBUS_ERR;
            }
            break;
        case CONNECT_TCP: {
            if (strcpy_s(option->socketOption.addr, sizeof(option->socketOption.addr), connInfo->socketInfo.addr) !=
                EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "strcpy_s failed");
                return SOFTBUS_ERR;
            }
            option->socketOption.port = connInfo->socketInfo.port;
            option->socketOption.protocol = connInfo->socketInfo.protocol;
            break;
        }
        default: {
            SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "unknown type");
            return SOFTBUS_ERR;
        }
    }
    return SOFTBUS_OK;
}

int32_t ConvertAuthConnInfoToOption(const AuthConnInfo *info, ConnectOption *option)
{
    if (info == NULL || option == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    switch (info->type) {
        case AUTH_LINK_TYPE_WIFI:
            option->type = CONNECT_TCP;
            if (strcpy_s(option->socketOption.addr, sizeof(option->socketOption.addr), info->info.ipInfo.ip) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy ip failed.");
                return SOFTBUS_MEM_ERR;
            }
            option->socketOption.port = info->info.ipInfo.port;
            option->socketOption.protocol = LNN_PROTOCOL_IP;
            option->socketOption.keepAlive = 1;
            break;
        case AUTH_LINK_TYPE_BR:
            option->type = CONNECT_BR;
            if (strcpy_s(option->brOption.brMac, sizeof(option->brOption.brMac),
                info->info.brInfo.brMac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy brMac failed.");
                return SOFTBUS_MEM_ERR;
            }
            break;
        case AUTH_LINK_TYPE_BLE:
            option->type = CONNECT_BLE;
            if (strcpy_s(option->bleOption.bleMac, sizeof(option->bleOption.bleMac),
                info->info.bleInfo.bleMac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy bleMac failed.");
                return SOFTBUS_MEM_ERR;
            }
            break;
        case AUTH_LINK_TYPE_P2P:
            option->type = CONNECT_TCP;
            if (strcpy_s(option->socketOption.addr, sizeof(option->socketOption.addr), info->info.ipInfo.ip) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy ip failed.");
                return SOFTBUS_MEM_ERR;
            }
            option->socketOption.port = info->info.ipInfo.port;
            option->socketOption.protocol = LNN_PROTOCOL_IP;
            option->socketOption.moduleId = AUTH_P2P;
            option->socketOption.keepAlive = 1;
            break;
        default:
            SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "unsupport link type, type = %d.", info->type);
            return SOFTBUS_INVALID_PARAM;
    }
    return SOFTBUS_OK;
}

int32_t ConvertOptionToAuthConnInfo(const ConnectOption *option, bool isAuthP2p, AuthConnInfo *info)
{
    if (option == NULL || info == NULL) {
        return SOFTBUS_INVALID_PARAM;
    }
    switch (option->type) {
        case CONNECT_TCP:
            info->type = isAuthP2p ? AUTH_LINK_TYPE_P2P : AUTH_LINK_TYPE_WIFI;
            if (strcpy_s(info->info.ipInfo.ip, sizeof(info->info.ipInfo.ip), option->socketOption.addr) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy ip failed.");
                return SOFTBUS_MEM_ERR;
            }
            info->info.ipInfo.port = option->socketOption.port;
            break;
        case CONNECT_BR:
            info->type = AUTH_LINK_TYPE_BR;
            if (strcpy_s(info->info.brInfo.brMac, sizeof(info->info.brInfo.brMac),
                option->brOption.brMac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy brMac failed.");
                return SOFTBUS_MEM_ERR;
            }
            break;
        case CONNECT_BLE:
            info->type = AUTH_LINK_TYPE_BLE;
            if (strcpy_s(info->info.bleInfo.bleMac, sizeof(info->info.bleInfo.bleMac),
                option->bleOption.bleMac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "copy bleMac failed.");
                return SOFTBUS_MEM_ERR;
            }
            break;
        default:
            SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "unknown type, type = %d.", option->type);
            return SOFTBUS_INVALID_PARAM;
    }
    return SOFTBUS_OK;
}

bool CompareConnectOption(const ConnectOption *option1, const ConnectOption *option2)
{
    if (option1 == NULL || option2 == NULL) {
        return false;
    }
    switch (option1->type) {
        case CONNECT_TCP:
            if (option2->type == CONNECT_TCP && option2->socketOption.protocol == option1->socketOption.protocol &&
                strcmp(option1->socketOption.addr, option2->socketOption.addr) == 0) {
                return true;
            }
            if (option2->type == CONNECT_TCP && option2->socketOption.protocol != LNN_PROTOCOL_IP) {
                char remoteIp[IP_LEN] = {0};
                if (GetRemoteIpByNodeAddr(remoteIp, sizeof(remoteIp), option2->socketOption.addr) != SOFTBUS_OK) {
                    SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "%s: get remote ip failed", __func__);
                    break;
                }
                if (strcmp(option1->socketOption.addr, remoteIp) == 0) {
                    return true;
                }
            }
            break;
        case CONNECT_BR:
            if (option2->type == CONNECT_BR &&
                strcmp(option1->brOption.brMac, option2->brOption.brMac) == 0) {
                return true;
            }
            break;
        case CONNECT_BLE:
            if (option2->type == CONNECT_BLE &&
                strcmp(option1->bleOption.bleMac, option2->bleOption.bleMac) == 0) {
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

void AnoonymousDid(char *outBuf, uint32_t len)
{
    if (outBuf == NULL || len == 0) {
        return;
    }
    uint32_t size = len > MAX_BYTE_RECORD ? MAX_BYTE_RECORD : len;
    uint32_t internal = 1;
    while ((internal * ANONYMOUS_INTEVER_LEN) < size) {
        uint32_t pos = internal * ANONYMOUS_INTEVER_LEN;
        outBuf[pos] = ANONYMOUS_CHAR;
        outBuf[--pos] = ANONYMOUS_CHAR;
        outBuf[--pos] = ANONYMOUS_CHAR;
        outBuf[--pos] = ANONYMOUS_CHAR;
        ++internal;
    }
}

void AuthPrintDfxMsg(uint32_t module, char *data, int len)
{
    if (!GetSignalingMsgSwitch()) {
        return;
    }
    if (data == NULL || len <= 0) {
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_ERROR, "invalid parameter");
        return;
    }
    if (!(module == MODULE_TRUST_ENGINE ||
        module == MODULE_AUTH_CONNECTION ||
        module == DATA_TYPE_DEVICE_ID ||
        module == DATA_TYPE_SYNC)) {
        return;
    }
    int32_t size = (len > MAX_BYTE_RECORD) ? (MAX_BYTE_RECORD - 1) : len;
    char outBuf[MAX_BYTE_RECORD + 1] = {0};
    if (ConvertBytesToHexString(outBuf, MAX_BYTE_RECORD, (const unsigned char *)data, size / 2) == SOFTBUS_OK) {
        AnoonymousDid(outBuf, strlen(outBuf));
        SoftBusLog(SOFTBUS_LOG_AUTH, SOFTBUS_LOG_INFO, "[signaling]:%s", outBuf);
    }
}