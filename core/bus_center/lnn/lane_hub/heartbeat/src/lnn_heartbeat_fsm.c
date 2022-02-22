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

#include "lnn_heartbeat_fsm.h"

#include <securec.h>
#include <string.h>

#include "bus_center_manager.h"
#include "lnn_connection_addr_utils.h"
#include "lnn_distributed_net_ledger.h"
#include "lnn_heartbeat_strategy.h"
#include "lnn_net_builder.h"
#include "lnn_node_info.h"
#include "lnn_node_weight.h"
#include "message_handler.h"

#include "softbus_adapter_crypto.h"
#include "softbus_adapter_mem.h"
#include "softbus_adapter_timer.h"
#include "softbus_conn_interface.h"
#include "softbus_errcode.h"
#include "softbus_log.h"
#include "softbus_utils.h"

typedef int32_t (*LnnHeartbeatHandler)(const SoftBusMessage *msg);

typedef struct {
    int32_t eventType;
    LnnHeartbeatHandler handler;
} LnnHeartbeatEventHandler;

typedef struct {
    int32_t eventNum;
    LnnHeartbeatEventHandler *eventHandler;
} LnnHeartbeatStateHandler;

static SoftBusHandler g_beatHandler = {0};
static int32_t g_curentState = -1;

static int32_t OnCheckDeviceStatus(const SoftBusMessage *msg);
static int32_t OnDetectDeviceLost(const SoftBusMessage *msg);
static int32_t OnElectAsMasterNode(const SoftBusMessage *msg);
static int32_t OnElectAsNormalNode(const SoftBusMessage *msg);
static int32_t OnHeartbeatStart(const SoftBusMessage *msg);
static int32_t OnHeartbeatStop(const SoftBusMessage *msg);
static int32_t OnMasterStateEnter(const SoftBusMessage *msg);
static int32_t OnMasterStateExit(const SoftBusMessage *msg);
static int32_t OnOneCycleBegin(const SoftBusMessage *msg);
static int32_t OnOneCycleEnd(const SoftBusMessage *msg);
static int32_t OnOneCycleTimeout(const SoftBusMessage *msg);
static int32_t OnRepeatCycle(const SoftBusMessage *msg);
static int32_t OnTryAsMasterNode(const SoftBusMessage *msg);

static LnnHeartbeatEventHandler g_noneHbStateHandler[] = {
    {EVENT_HB_ENTER, OnHeartbeatStop},
    {EVENT_HB_START, OnHeartbeatStart},
    {EVENT_HB_EXIT, NULL}
};

static LnnHeartbeatEventHandler g_normalNodeStateHandler[] = {
    {EVENT_HB_ENTER, OnTryAsMasterNode},
    {EVENT_HB_START, OnHeartbeatStart},
    {EVENT_HB_ONCE_BEGIN, OnOneCycleBegin},
    {EVENT_HB_DEVICE_LOST, OnDetectDeviceLost},
    {EVENT_HB_CHECK_DEV, OnCheckDeviceStatus},
    {EVENT_HB_AS_MASTER_NODE, OnElectAsMasterNode},
    {EVENT_HB_AS_NORMAL_NODE, OnElectAsNormalNode},
    {EVENT_HB_ONCE_END, OnOneCycleEnd},
    {EVENT_HB_STOP, OnHeartbeatStop},
    {EVENT_HB_TIMEOUT, OnOneCycleTimeout},
    {EVENT_HB_EXIT, NULL}
};

static LnnHeartbeatEventHandler g_masterNodeStateHandler[] = {
    {EVENT_HB_ENTER, OnMasterStateEnter},
    {EVENT_HB_START, OnHeartbeatStart},
    {EVENT_HB_ONCE_BEGIN, OnOneCycleBegin},
    {EVENT_HB_DEVICE_LOST, OnDetectDeviceLost},
    {EVENT_HB_CHECK_DEV, OnCheckDeviceStatus},
    {EVENT_HB_REPEAT_CYCLE, OnRepeatCycle},
    {EVENT_HB_AS_MASTER_NODE, OnElectAsMasterNode},
    {EVENT_HB_AS_NORMAL_NODE, OnElectAsNormalNode},
    {EVENT_HB_ONCE_END, OnOneCycleEnd},
    {EVENT_HB_STOP, OnHeartbeatStop},
    {EVENT_HB_TIMEOUT, OnOneCycleTimeout},
    {EVENT_HB_EXIT, OnMasterStateExit}
};

static LnnHeartbeatStateHandler g_beatStatHandler[] = {
    [STATE_HB_NONE_INDEX] = {
        .eventNum = sizeof(g_noneHbStateHandler) / sizeof(LnnHeartbeatEventHandler),
        .eventHandler = g_noneHbStateHandler,
    },
    [STATE_HB_NORMAL_NODE_INDEX] = {
        .eventNum = sizeof(g_normalNodeStateHandler) / sizeof(LnnHeartbeatEventHandler),
        .eventHandler = g_normalNodeStateHandler,
    },
    [STATE_HB_MASTER_NODE_INDEX] = {
        .eventNum = sizeof(g_masterNodeStateHandler) / sizeof(LnnHeartbeatEventHandler),
        .eventHandler = g_masterNodeStateHandler,
    }
};

static void FreeHbHandlerMsg(SoftBusMessage *msg)
{
    if (msg != NULL) {
        if (msg->obj != NULL) {
            SoftBusFree(msg->obj);
        }
        SoftBusFree(msg);
    }
}

static SoftBusMessage *CreateHbHandlerMsg(int32_t what, uint64_t arg1, uint64_t arg2, void *obj)
{
    SoftBusMessage *msg = (SoftBusMessage *)SoftBusCalloc(sizeof(SoftBusMessage));
    if (msg == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB create msg calloc fail, what=%d", what);
        return NULL;
    }
    msg->what = what;
    msg->arg1 = arg1;
    msg->arg2 = arg2;
    msg->handler = &g_beatHandler;
    msg->FreeMessage = FreeHbHandlerMsg;
    msg->obj = obj;
    return msg;
}

static void HbFsmTransactState(int32_t fromState, int32_t toState, const SoftBusMessage *msg)
{
    int32_t eventNum = g_beatStatHandler[fromState].eventNum;
    LnnHeartbeatEventHandler *eventHandler = g_beatStatHandler[fromState].eventHandler;

    if (eventHandler[eventNum - 1].eventType != EVENT_HB_EXIT) {
        return;
    }
    if ((eventHandler[eventNum - 1].handler != NULL) && (eventHandler[eventNum - 1].handler(msg) < 0)) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB FSM process exit err, stateIndex=%d", fromState);
        return;
    }
    eventHandler = g_beatStatHandler[toState].eventHandler;
    if (eventHandler[0].eventType != EVENT_HB_ENTER) {
        return;
    }
    if ((eventHandler[0].handler != NULL) && (eventHandler[EVENT_HB_ENTER].handler(msg) < 0)) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB FSM process enter err, stateIndex=%d", toState);
        return;
    }
}

static void HbMsgHandler(SoftBusMessage *msg)
{
    int32_t actIdx, eventNum, eventType, nextStatus, ret;

    if (msg == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB msg handler get invalid param");
        return;
    }
    if (g_curentState < 0 || g_curentState >= STATE_HB_INDEX_MAX) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB unknow state or not init yet");
        return;
    }
    eventType = msg->what;
    nextStatus = g_curentState;
    eventNum = g_beatStatHandler[g_curentState].eventNum;
    LnnHeartbeatEventHandler *eventHandler = g_beatStatHandler[g_curentState].eventHandler;
    for (actIdx = 0; actIdx < eventNum; ++actIdx) {
        if (eventHandler[actIdx].eventType == eventType) {
            ret = (eventHandler[actIdx].handler)(msg);
            if (ret < 0) {
                SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB FSM process msg(%d) fail, ret=%d", eventType, ret);
                return;
            }
            SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "HB FSM process msg(%d) done, ret=%d, nowstatus=%d",
                eventType, ret, g_curentState);
            nextStatus = ret;
            break;
        }
    }
    if (actIdx == eventNum) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_WARN, "HB no handler what=%d in status=%d", eventType, g_curentState);
    }
    if (nextStatus != g_curentState) {
        HbFsmTransactState(g_curentState, nextStatus, msg);
    }
    g_curentState = nextStatus;
}

static int32_t PostMsgToHbHandler(int32_t what, uint64_t arg1, uint64_t arg2, void *obj)
{
    SoftBusMessage *msg = CreateHbHandlerMsg(what, arg1, arg2, obj);
    if (msg == NULL) {
        return SOFTBUS_ERR;
    }
    g_beatHandler.looper->PostMessage(g_beatHandler.looper, msg);
    return SOFTBUS_OK;
}

static int32_t PostDelayMsgToHbHandler(int32_t what, uint64_t arg1, uint64_t arg2, void *obj, uint64_t delayMillis)
{
    SoftBusMessage *msg = CreateHbHandlerMsg(what, arg1, arg2, obj);
    if (msg == NULL) {
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "HB post delay msg, what=%d delayMillis=%llu msec",
        what, delayMillis);
    g_beatHandler.looper->PostMessageDelay(g_beatHandler.looper, msg, delayMillis);
    return SOFTBUS_OK;
}

/* remove message when return 0, else return 1 */
static int32_t RemoveHbMsgFunc(const SoftBusMessage *msg, void *args)
{
    if (msg == NULL || args == NULL) {
        return 1;
    }
    SoftBusMessage *delMsg = (SoftBusMessage *)args;
    if (delMsg->obj == NULL) {
        if (msg->what == delMsg->what) {
            return 0;
        }
    } else {
        if ((msg->obj != NULL) && (msg->what == delMsg->what) && (msg->arg2 == delMsg->arg2) &&
            (strcmp((const char *)msg->obj, (const char *)delMsg->obj) == 0)) {
            return 0;
        }
    }
    return 1;
}

int32_t LnnPostMsgToHbFsm(int32_t eventType, void *obj)
{
    if (eventType < 0 || eventType >= EVENT_HB_MAX) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB post msg get invalid param, what=%d", eventType);
        return SOFTBUS_INVALID_PARAM;
    }
    return PostMsgToHbHandler(eventType, 0, 0, obj);
}

int32_t LnnPostDelayMsgToHbFsm(int32_t eventType, void *obj, uint64_t delayMillis)
{
    if (eventType < 0 || eventType >= EVENT_HB_MAX || delayMillis < 0) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB post delay msg get invalid param, what=%d", eventType);
        return SOFTBUS_INVALID_PARAM;
    }
    return PostDelayMsgToHbHandler(eventType, 0, 0, obj, delayMillis);
}

int32_t LnnRemoveHbFsmMsg(int32_t eventType, uint64_t para, void *obj)
{
    if (eventType < 0 || eventType >= EVENT_HB_MAX) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB remove msg get invalid param, what=%d", eventType);
        return SOFTBUS_INVALID_PARAM;
    }
    SoftBusMessage *msg = CreateHbHandlerMsg(eventType, 0, para, obj);
    if (msg == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB remove msg create msg fail, what=%d", eventType);
        return SOFTBUS_ERR;
    }
    g_beatHandler.looper->RemoveMessageCustom(g_beatHandler.looper, &g_beatHandler, RemoveHbMsgFunc, msg);
    return SOFTBUS_OK;
}

int32_t LnnHbCheckDevStatus(ConnectionAddrType type, uint64_t delayMillis)
{
    return PostDelayMsgToHbHandler(EVENT_HB_CHECK_DEV, 0, (uint64_t)type, NULL, delayMillis);
}

int32_t LnnHbAsNormalNode(void)
{
    return PostMsgToHbHandler(EVENT_HB_AS_NORMAL_NODE, 0, 0, NULL);
}

int32_t LnnHbRelayToMaster(ConnectionAddrType type)
{
    if (LnnHbAsNormalNode() != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB perform as normal node fail");
        return SOFTBUS_ERR;
    }
    return PostMsgToHbHandler(EVENT_HB_ONCE_BEGIN, 0, (uint64_t)type, NULL);
}

int32_t LnnHbProcessDeviceLost(const char *networkId, ConnectionAddrType addrType, uint64_t delayMillis)
{
    if (networkId == NULL || addrType > CONNECTION_ADDR_MAX || delayMillis < 0) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB detect dev lost get invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    char *tempId = (char *)SoftBusCalloc(NETWORK_ID_BUF_LEN);
    if (tempId == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB networkId malloc err");
        return SOFTBUS_MALLOC_ERR;
    }
    if (strcpy_s(tempId, NETWORK_ID_BUF_LEN, networkId) != EOK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB networkId strcpy_s err");
        SoftBusFree(tempId);
        return SOFTBUS_ERR;
    }
    return PostDelayMsgToHbHandler(EVENT_HB_DEVICE_LOST, 0, (uint64_t)addrType, (void *)tempId, delayMillis);
}

int32_t LnnHbFsmStart(int32_t stateIndex, uint64_t delayMillis)
{
    if (stateIndex < 0 || stateIndex >= STATE_HB_INDEX_MAX) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB fsm start get invalid param");
        return SOFTBUS_INVALID_PARAM;
    }
    return PostDelayMsgToHbHandler(EVENT_HB_START, (uint64_t)stateIndex, 0, NULL, delayMillis);
}

int32_t LnnHbFsmStop(uint64_t delayMillis)
{
    return PostDelayMsgToHbHandler(EVENT_HB_STOP, 0, 0, NULL, delayMillis);
}

int32_t LnnHbFsmInit(void)
{
    g_curentState = STATE_HB_NONE_INDEX;
    g_beatHandler.name = "heartbeat_handler";
    g_beatHandler.HandleMessage = HbMsgHandler;
    g_beatHandler.looper = GetLooper(LOOP_TYPE_DEFAULT);
    if (g_beatHandler.looper == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB get looper fail");
        return SOFTBUS_ERR;
    }
    return SOFTBUS_OK;
}

void LnnHbFsmDeinit(void)
{
    g_curentState = -1;
}

static int32_t OnTryAsMasterNode(const SoftBusMessage *msg)
{
    (void)msg;
    GearMode gearMode;

    LnnDumpHbMgrUpdateList();
    LnnDumpHbOnlineNodeList();
    if (g_curentState == STATE_HB_MASTER_NODE_INDEX) {
        return STATE_HB_MASTER_NODE_INDEX;
    }
    if (LnnRemoveHbFsmMsg(EVENT_HB_AS_MASTER_NODE, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    if (LnnRemoveHbFsmMsg(EVENT_HB_REPEAT_CYCLE, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    if (LnnGetHeartbeatGearMode(&gearMode) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    uint64_t delayMillis = (uint64_t)gearMode.modeCycle * HB_TIME_FACTOR + HB_ENABLE_DELAY_LEN;
    if (LnnPostDelayMsgToHbFsm(EVENT_HB_AS_MASTER_NODE, NULL, delayMillis) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "heartbeat(HB) try as master node in %llu mecs", delayMillis);
    return STATE_HB_NORMAL_NODE_INDEX;
}

static int32_t OnMasterStateEnter(const SoftBusMessage *msg)
{
    (void)msg;
    char udid[UDID_BUF_LEN] = {0};

    if (LnnGetLocalStrInfo(STRING_KEY_DEV_UDID, udid, UDID_BUF_LEN) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB get local udid err");
        return SOFTBUS_ERR;
    }
    if (LnnSetLocalStrInfo(STRING_KEY_MASTER_NODE_UDID, udid) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB set local master node udid err");
        return SOFTBUS_ERR;
    }
    if (LnnSetLocalNumInfo(NUM_KEY_MASTER_NODE_WEIGHT, LnnGetLocalWeight()) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB set local master node weight err");
        return SOFTBUS_ERR;
    }
    if (LnnRemoveHbFsmMsg(EVENT_HB_REPEAT_CYCLE, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    if (LnnPostDelayMsgToHbFsm(EVENT_HB_REPEAT_CYCLE, NULL, HB_ENABLE_DELAY_LEN) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    return STATE_HB_MASTER_NODE_INDEX;
}

static int32_t OnMasterStateExit(const SoftBusMessage *msg)
{
    (void)msg;

    if (LnnRemoveHbFsmMsg(EVENT_HB_REPEAT_CYCLE, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    return STATE_HB_MASTER_NODE_INDEX;
}

static int32_t OnRepeatCycle(const SoftBusMessage *msg)
{
    (void)msg;
    GearMode gearMode;

    LnnDumpHbMgrUpdateList();
    LnnDumpHbOnlineNodeList();
    if (PostMsgToHbHandler(EVENT_HB_ONCE_BEGIN, 0, CONNECTION_ADDR_MAX, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    if (LnnPostDelayMsgToHbFsm(EVENT_HB_TIMEOUT, NULL, HB_ONE_CYCLE_TIMEOUT_LEN) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    if (LnnGetHeartbeatGearMode(&gearMode) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    uint64_t delayMillis = (uint64_t)gearMode.modeCycle * HB_TIME_FACTOR;
    if (LnnPostDelayMsgToHbFsm(EVENT_HB_REPEAT_CYCLE, NULL, delayMillis) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "heartbeat(HB) perform as master node");
    return STATE_HB_MASTER_NODE_INDEX;
}

static int32_t OnElectAsMasterNode(const SoftBusMessage *msg)
{
    (void)msg;
    g_curentState = STATE_HB_NONE_INDEX;

    if (LnnRemoveHbFsmMsg(EVENT_HB_AS_MASTER_NODE, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "heartbeat(HB) elect as master node");
    return STATE_HB_MASTER_NODE_INDEX;
}

static int32_t OnElectAsNormalNode(const SoftBusMessage *msg)
{
    (void)msg;
    g_curentState = STATE_HB_NONE_INDEX;

    if (LnnRemoveHbFsmMsg(EVENT_HB_AS_MASTER_NODE, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "heartbeat(HB) elect as normal node");
    return STATE_HB_NORMAL_NODE_INDEX;
}

static int32_t OnOneCycleBegin(const SoftBusMessage *msg)
{
    if (LnnHbMgrOneCycleBegin() != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB start mgr to perform one cycle fail");
        (void)LnnRemoveHbFsmMsg(EVENT_HB_TIMEOUT, 0, NULL);
        return SOFTBUS_ERR;
    }
    if (LnnPostDelayMsgToHbFsm(EVENT_HB_ONCE_END, NULL, HB_ONE_CYCLE_LEN) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    if (LnnHbCheckDevStatus((ConnectionAddrType)msg->arg2, HB_CHECK_DELAY_LEN) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    return g_curentState;
}

static int32_t OnOneCycleEnd(const SoftBusMessage *msg)
{
    (void)msg;

    if (LnnHbMgrOneCycleEnd() != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "stop once HB adv fail");
        return SOFTBUS_ERR;
    }
    return g_curentState;
}

static int32_t OnHeartbeatStart(const SoftBusMessage *msg)
{
    g_curentState = STATE_HB_NONE_INDEX;
    int32_t stateIndex = (int32_t)msg->arg1;
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "heartbeat(HB) start in status: %d", stateIndex);
    return stateIndex;
}

static int32_t OnHeartbeatStop(const SoftBusMessage *msg)
{
    (void)msg;

    if (LnnHbMgrStop() != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB stop manager fail");
        return SOFTBUS_ERR;
    }
    SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "heartbeat(HB) process stop.");
    return STATE_HB_NONE_INDEX;
}

static int32_t OnOneCycleTimeout(const SoftBusMessage *msg)
{
    (void)msg;

    if (LnnRemoveHbFsmMsg(EVENT_HB_ONCE_BEGIN, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    if (LnnRemoveHbFsmMsg(EVENT_HB_ONCE_END, 0, NULL) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    return g_curentState;
}

static bool HbCheckActiveConn(ConnectionAddrType addrType, const char *networkId)
{
    ConnectOption option = {0};
    NodeInfo *nodeInfo = NULL;
    const char *mac = NULL;

    switch (addrType) {
        case CONNECTION_ADDR_WLAN:
        case CONNECTION_ADDR_ETH:
        case CONNECTION_ADDR_BR:
        /* heartbeat dont support this medium type yet, so dont take the dev offline */
            return true;
        case CONNECTION_ADDR_MAX:
        case CONNECTION_ADDR_BLE:
            nodeInfo = LnnGetNodeInfoById(networkId, CATEGORY_NETWORK_ID);
            if (nodeInfo == NULL) {
                SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "HB not find node, no need to notify lost");
                return true;
            }
            mac = LnnGetBtMac(nodeInfo);
            if (mac == NULL) {
                SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB get bt mac err");
                return true;
            }
            option.type = CONNECT_BR;
            if (strcpy_s(option.info.brOption.brMac, BT_MAC_LEN, mac) != EOK) {
                SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB strcpy_s bt mac err");
                return true;
            }
            break;
        default:
            break;
    }
    return CheckActiveConnection(&option);
}

static int32_t OnDetectDeviceLost(const SoftBusMessage *msg)
{
    ConnectionAddrType addrType = (ConnectionAddrType)msg->arg2;
    const char *networkId = (const char *)msg->obj;

    if (networkId == NULL) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB device networkId is null");
        return SOFTBUS_ERR;
    }
    if (HbCheckActiveConn(addrType, networkId)) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_WARN, "HB cannot offline dev, set new offline check begin");
        if (LnnOfflineTimingByHeartbeat(networkId, addrType) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB set new offline check err");
            return SOFTBUS_ERR;
        }
        return g_curentState;
    }
    if (addrType == CONNECTION_ADDR_MAX) {
    /* heartbeat dont support medium type except ble now, so only offline ble devices */
        if (LnnRequestLeaveSpecific(networkId, CONNECTION_ADDR_BLE) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB notify device lost fail");
            return SOFTBUS_ERR;
        }
        return g_curentState;
    }
    if (LnnRequestLeaveSpecific(networkId, addrType) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB notify device lost fail");
        return SOFTBUS_ERR;
    }
    return g_curentState;
}

static int32_t OnCheckDeviceStatus(const SoftBusMessage *msg)
{
    int32_t infoNum, i;
    GearMode gearMode;
    SoftBusSysTime times;
    uint64_t nowTime, oldTimeStamp, offlineMillis;
    NodeBasicInfo *info = NULL;

    DiscoveryType discType = LnnGetDiscoveryType(msg->arg2);
    if (LnnGetAllOnlineNodeInfo(&info, &infoNum) != SOFTBUS_OK) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB get node info fail");
        return SOFTBUS_ERR;
    }
    if (info == NULL || infoNum == 0) {
        SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "HB no online node");
        return g_curentState;
    }
    SoftBusGetTime(&times);
    if (LnnGetHeartbeatGearMode(&gearMode) != SOFTBUS_OK) {
        return SOFTBUS_ERR;
    }
    offlineMillis = (uint64_t)gearMode.modeCycle * HB_TIME_FACTOR + HB_ENABLE_DELAY_LEN;
    nowTime = (uint64_t)times.sec * HB_TIME_FACTOR + (uint64_t)times.usec / HB_TIME_FACTOR;
    for (i = 0; i < infoNum; i++) {
        NodeInfo *nodeInfo = LnnGetNodeInfoById(info[i].networkId, CATEGORY_NETWORK_ID);
        if (nodeInfo == NULL || (msg->arg2 != CONNECTION_ADDR_MAX && !LnnHasDiscoveryType(nodeInfo, discType))) {
            continue;
        }
        if (LnnGetDistributedHeartbeatTimestamp(info[i].networkId, &oldTimeStamp) != SOFTBUS_OK) {
            SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB get timeStamp err, nodeInfo i=%d", i);
            continue;
        }
        if ((nowTime - oldTimeStamp) > offlineMillis) {
            SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_INFO, "HB notify nodeInfo i=%d offline, timestamp:%llu, now:%llu",
                i, oldTimeStamp, nowTime);
            if (LnnRemoveHbFsmMsg(EVENT_HB_DEVICE_LOST, msg->arg2, info[i].networkId) != SOFTBUS_OK) {
                SoftBusFree(info);
                return SOFTBUS_ERR;
            }
            if (LnnHbProcessDeviceLost(info[i].networkId, (ConnectionAddrType)msg->arg2, 0) != SOFTBUS_OK) {
                SoftBusLog(SOFTBUS_LOG_LNN, SOFTBUS_LOG_ERROR, "HB process dev lost err, nodeInfo i=%d", i);
                SoftBusFree(info);
                return SOFTBUS_ERR;
            }
        }
    }
    SoftBusFree(info);
    return g_curentState;
}
