
/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <pixelhealth/HealthHelper.h>
#include <pixelhealth/LowBatteryShutdownMetrics.h>
#include <pixelhealth/StatsHelper.h>

namespace hardware {
namespace google {
namespace pixel {
namespace health {

using aidl::android::hardware::health::BatteryStatus;
using aidl::android::hardware::health::HealthInfo;
using android::base::GetProperty;
using android::base::ReadFileToString;
using android::base::SetProperty;

LowBatteryShutdownMetrics::LowBatteryShutdownMetrics(const char *const voltage_avg,
                                                     const char *const persist_prop)
    : kVoltageAvg(voltage_avg), kPersistProp(persist_prop) {
    prop_written_ = false;
    prop_empty_ = false;
}

bool LowBatteryShutdownMetrics::uploadVoltageAvg(void) {
    std::string prop_contents = GetProperty(kPersistProp, "");
    LOG(INFO) << kPersistProp << " property contents: " << prop_contents;
    if (prop_contents.size() == 0) {  // we don't have anything to upload
        prop_empty_ = true;
        return false;
    }

    std::shared_ptr<IStats> stats_client = getStatsService();
    if (!stats_client) {
        LOG(ERROR) << "Unable to connect to IStats service";
        return false;
    }

    // Process and upload comma-delimited last voltage values
    int32_t voltage_avg;
    for (const auto &item : android::base::Split(prop_contents, ",")) {
        if (!(voltage_avg = stoi(item))) {
            LOG(ERROR) << "Couldn't process voltage value " << item;
            continue;
        }
        LOG(INFO) << "Uploading voltage_avg: " << std::to_string(voltage_avg);
        reportBatteryCausedShutdown(stats_client, voltage_avg);
    }

    // Clear property now that we've uploaded its contents
    SetProperty(kPersistProp, "");
    return true;
}

bool LowBatteryShutdownMetrics::saveVoltageAvg(void) {
    std::string voltage_avg;
    std::string prop_contents;

    if (!ReadFileToString(kVoltageAvg, &voltage_avg)) {
        LOG(ERROR) << "Can't read the Maxim fuel gauge average voltage value";
        return false;
    }
    voltage_avg = ::android::base::Trim(voltage_avg);
    prop_contents = GetProperty(kPersistProp, "");

    // Comma delimit additional values
    if (prop_contents.size() > 0)
        prop_contents += ",";
    prop_contents += voltage_avg;

    LOG(INFO) << "Saving \"" << prop_contents << "\" to " << kPersistProp;

    return SetProperty(kPersistProp, prop_contents);
}

void LowBatteryShutdownMetrics::logShutdownVoltage(const HealthInfo &health_info) {
    // If we're about to shut down due to low battery, save voltage_avg
    if (!prop_written_ && health_info.batteryLevel == 0 &&
        health_info.batteryStatus == BatteryStatus::DISCHARGING) {
        prop_written_ = saveVoltageAvg();
    } else if (!prop_empty_) {  // We have data to upload
        uploadVoltageAvg();
    }

    return;
}

void LowBatteryShutdownMetrics::logShutdownVoltage(struct android::BatteryProperties *props) {
    logShutdownVoltage(ToHealthInfo(props));
}

}  // namespace health
}  // namespace pixel
}  // namespace google
}  // namespace hardware
