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

#include "setfilesendlistener_fuzzer.h"
#include <cstddef>
#include <cstdint>
#include "inner_session.h"
#include "session.h"
#include "softbus_utils.h"

namespace OHOS {
void SetFileSendListenerTest(const uint8_t* data, size_t size)
{
    if ((data == nullptr) || (size <= 0)) {
        return;
    }
    char *sessionName = nullptr;
    IFileSendListener *recvListener = nullptr;
    SetFileSendListener((const char *)data, sessionName, recvListener);
}
} // namespace OHOS

/* Fuzzer entry point */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    OHOS::SetFileSendListenerTest(data, size);

    return 0;
}