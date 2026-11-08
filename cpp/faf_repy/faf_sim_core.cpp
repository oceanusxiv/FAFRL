#include "faf_sim_core.hpp"

#include <format>
#include <memory>
#include <print>
#include <string>

#include "gpg/core/utils/Logging.h"
#include "legacy/math/X87Math.h"
#include "ensure_sim_thread.hpp"
#include "moho/ai/IAiBuilder.h"
#include "moho/command/CCommandDb.h"
#include "moho/command/SSTICommandIssueData.h"
#include "moho/entity/EntityDb.h"
#include "moho/misc/LaunchInfoBase.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/LaunchInfoBuilder.h"
#include "moho/resource/ResourceManager.h"
#include "moho/resource/blueprints/RUnitBlueprint.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/sim/CWldMap.h"
#include "moho/sim/RRuleGameRules.h"
#include "moho/sim/Sim.h"
#include "moho/sim/WldSessionInfo.h"
#include "moho/unit/core/IUnit.h"
#include "remote/VisibilityOracle.h"

namespace faf::py {
namespace {
auto DieHandler(const char *const message) {
    std::println(stderr, "Fatal engine error: {}", message != nullptr ? message : "null");
}

auto CollectUnitObservation(const moho::Unit &unit) -> UnitObservation {
    const auto &evd = unit.mVarDat;
    const auto &uvd = unit.mUnitVarDat;
    const auto &pos = unit.GetPosition();
    const auto &vel = unit.GetVelocity();
    const auto army_idx = unit.GetArmyIndex();

    const auto flags = static_cast<std::uint8_t>(
        (uvd.mIsPaused ? UnitObservationFlags::kPaused : 0u) |
        (uvd.mAutoMode ? UnitObservationFlags::kAutoMode : 0u) |
        (uvd.mAutoSurfaceMode ? UnitObservationFlags::kAutoSurfaceMode : 0u) |
        (uvd.mIsBusy ? UnitObservationFlags::kBusy : 0u) |
        (uvd.mRepeatQueue ? UnitObservationFlags::kRepeatQueue : 0u) |
        (uvd.mIsValidTarget ? UnitObservationFlags::kValidTarget : 0u) |
        (uvd.mOverchargePaused ? UnitObservationFlags::kOverchargePaused : 0u) |
        (evd.mIsBeingBuilt ? UnitObservationFlags::kBeingBuilt : 0u));
    return {.entity_id = static_cast<std::int32_t>(unit.GetEntityId()),
            .bp_ordinal = unit.BluePrint != nullptr
                              ? static_cast<std::uint16_t>(unit.BluePrint->mCategoryBitIndex + 1u)
                              : static_cast<std::uint16_t>(0u),
            .army_index = (army_idx >= 0 && army_idx < 255) ? static_cast<std::uint8_t>(army_idx)
                                                            : std::uint8_t{0xFFu},
            .unit_flags = flags,
            .pos_x = pos.x,
            .pos_y = pos.y,
            .pos_z = pos.z,
            .vel_x = vel.x,
            .vel_y = vel.y,
            .vel_z = vel.z,
            .health = evd.mHealth,
            .max_health = evd.mMaxHealth,
            .fraction_complete = evd.mFractionComplete,
            .shield_ratio = uvd.mShieldRatio,
            .fuel_ratio = uvd.mFuelRatio,
            .work_progress = uvd.mWorkProgress,
            .command_queue_len = static_cast<std::uint32_t>(uvd.mCommands.size()),
            .script_bits = uvd.mScriptbits,
            .unit_state_mask = uvd.mUnitStates};
}

auto CollectEconomyObservation(const moho::CArmyImpl &army) -> EconomyObservation {
    const auto &vd = army.mVarDat;
    const auto &totals = vd.mEconomyTotals;
    return {
        .mass_stored = totals.mStored.MASS,
        .energy_stored = totals.mStored.ENERGY,
        .mass_income = totals.mIncome.MASS,
        .energy_income = totals.mIncome.ENERGY,
        .mass_requested = totals.mLastUseRequested.MASS,
        .energy_requested = totals.mLastUseRequested.ENERGY,
        .mass_actual = totals.mLastUseActual.MASS,
        .energy_actual = totals.mLastUseActual.ENERGY,
        .mass_max_storage = static_cast<float>(totals.mMaxStorage.MASS),
        .energy_max_storage = static_cast<float>(totals.mMaxStorage.ENERGY),
        .is_out_of_game = vd.mIsOutOfGame,
        .pad = {0, 0, 0},
    };
}

auto ResolveBlueprintDisplayName(moho::RRuleGameRules &rules, const LuaPlus::LuaObject &blueprints,
                                 const moho::RBlueprint &bp) -> std::string {
    moho::RResId res_id{.name = bp.mBlueprintId};
    if (const auto unit_bp = rules.GetUnitBlueprint(res_id);
        unit_bp != nullptr && !unit_bp->Display.DisplayName.empty()) {
        return unit_bp->Display.DisplayName.c_str();
    }

    if (blueprints.IsTable() && !bp.mBlueprintId.empty()) {
        const auto entry = blueprints[bp.mBlueprintId.c_str()];
        if (entry.IsTable()) {
            const auto general = entry["General"];
            if (general.IsTable()) {
                const auto unit_name = general["UnitName"];
                if (unit_name.IsString()) {
                    if (const auto s = unit_name.GetString(); s != nullptr && s[0] != '\0') {
                        return s;
                    }
                }
            }
        }
    }

    if (!bp.mDescription.empty()) {
        return bp.mDescription.c_str();
    }
    return bp.mBlueprintId.c_str();
}
} // namespace

auto FafSimCore::Init(const std::string &game_data_path) -> void {
    static std::once_flag init_once;
    std::call_once(init_once, [&]() {
        gpg::SetDieHandler(&DieHandler);
        static gpg::StreamLogTarget stderr_log(std::cerr, 1u);

        (void)SetSse2Mode(moho::CFG_GetArgOption("/sse2", 0, nullptr) ? 1 : 0);

        gpg::REF_RegisterAllTypes();
        moho::RES_EnsureResourceManager();
        moho::RES_ActivatePendingFactories();

        const msvc8::string data_path_script_name("init_faf.lua");
        if (!moho::DISK_SetupDataAndSearchPaths(data_path_script_name, game_data_path)) {
            throw std::runtime_error(
                std::format("FafRuntime::Init: moho::DISK_SetupDataAndSearchPaths "
                            "failed for {} and {}",
                            game_data_path, data_path_script_name.c_str()));
        }
    });
}

auto FafSimCore::ShutdownSim() -> void {
    heightmap_.clear();
    unit_obs_buffer_.clear();
    econ_obs_buffer_.clear();

    if (!sim_) {
        return;
    }

    if (sim_) {
        EnsureSimThread();
        sim_->Shutdown();
        SyncSim();
        sim_.reset();
    }
}

auto FafSimCore::CollectObservations() -> void {
    unit_obs_buffer_.clear();
    reclaim_obs_buffer_.clear();
    if (!sim_) {
        econ_obs_buffer_.clear();
        return;
    }

    for (moho::CUnitIterAllArmies it(sim_.get()); it.mItr != it.mEnd; it.Next()) {
        if (const auto unit = it.mCur;
            unit != nullptr && !unit->IsDead() && !unit->DestroyQueued()) {
            unit_obs_buffer_.emplace_back(CollectUnitObservation(*unit));
        }
    }

    const auto army_count = sim_->mArmiesList.size();
    econ_obs_buffer_.resize(army_count);
    for (std::size_t i = 0; i < army_count; ++i) {
        const auto army = sim_->mArmiesList[i];
        econ_obs_buffer_[i] =
            army != nullptr ? CollectEconomyObservation(*army) : EconomyObservation{};
    }
}

auto FafSimCore::ExtractBlueprintDisplayNames() -> void {
    if (!sim_ || !sim_->mRules) {
        return;
    }

    auto count = sim_->mRules->AssignNextOrdinal();
    if (count == 0) {
        return;
    }

    const auto lua_bps =
        sim_->mLuaState ? sim_->mLuaState->GetGlobal("__blueprints") : LuaPlus::LuaObject{};

    bp_names_.resize(count + 1, "null");
    for (std::size_t i = 0; i < count; ++i) {
        if (const auto *bp = sim_->mRules->GetBlueprintFromOrdinal(i); bp != nullptr) {
            bp_names_[i + 1] = ResolveBlueprintDisplayName(*sim_->mRules, lua_bps, *bp);
        }
    }
}

} // namespace faf::py