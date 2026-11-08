#pragma once

#include "ensure_sim_thread.hpp"
#include "faf_sim_core.hpp"
#include "moho/ai/IAiBuilder.h"
#include "moho/command/CCommandDb.h"
#include "moho/command/SSTICommandIssueData.h"
#include "moho/entity/EntityDb.h"
#include "moho/misc/LaunchInfoBase.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/LaunchInfoBuilder.h"
#include "moho/resource/blueprints/RUnitBlueprint.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/sim/CWldMap.h"
#include "moho/sim/CWldSessionLoaderImpl.h"
#include "moho/sim/FafGameVersion.h"
#include "moho/sim/RRuleGameRules.h"
#include "moho/sim/Sim.h"
#include "moho/unit/core/IUnit.h"
#include "remote/InboundIds.h"
#include "remote/VisibilityOracle.h"
#include <format>
#include <memory>
#include <string>
#include <thread>

namespace faf::py {
namespace {
constexpr moho::CommandSourceId kInvalidCommandSource = 0xFFu;
constexpr std::uint32_t kCmdSerialMask = 0x00FFFFFFu;
constexpr std::uint32_t kUnitEntityFamilyNibble = 0u;

struct HeadlessRoster final : moho::ILaunchParticipants {
    moho::CLaunchRoster inner{-1, {}, {}};

    [[nodiscard]] std::int32_t LocalUid() const override { return -1; }

    void AssignClientIndex(std::int32_t &clientIndex, std::int32_t ownerId, const char *name,
                           std::int32_t &localIndex) override {
        localIndex = 0;
        inner.AssignClientIndex(clientIndex, ownerId, name, localIndex);
    }

    std::uint32_t AssignCommandSource(std::int32_t timeouts, std::int32_t ownerId,
                                      msvc8::vector<moho::SSTICommandSource> &commandSources,
                                      std::uint32_t &localSource) override {
        return inner.AssignCommandSource(timeouts, ownerId, commandSources, localSource);
    }
};

class CommandValidator final : public faf::remote::CCommandIdRewriter {
  public:
    CommandValidator(moho::Sim &sim, const moho::CArmyImpl &army, const int army_index)
        : CCommandIdRewriter(sim), sim_(sim), army_(army), army_index_(army_index),
          fog_of_war_(army.AiReconDb != nullptr && army.AiReconDb->ReconGetFogOfWar()) {}

  protected:
    [[nodiscard("")]] bool RewriteEntity(std::uint32_t &id) override {
        if (sim_.mEntityDB == nullptr) {
            return false;
        }

        auto const entity = sim_.mEntityDB->FindEntityById(id);
        if (entity == nullptr) {
            return false;
        }
        if (entity->IsUnit() == nullptr && entity->IsReconBlip() == nullptr &&
            entity->IsProp() == nullptr) {
            return false;
        }
        return !fog_of_war_ || faf::remote::CanViewSee(sim_, army_index_, *entity);
    }

    [[nodiscard("")]] bool RewriteCommand(std::uint32_t &id) override {
        return army_.mVarDat.mValidCommandSources.Contains(id >> 24u);
    }

  private:
    moho::Sim &sim_;
    const moho::CArmyImpl &army_;
    int army_index_;
    bool fog_of_war_;
};
} // namespace

class FafRuntime : public FafSimCore {
  public:
    struct Config {
        std::string game_data_path; // Directory containing init.lua.
        std::string lua_config_str;
        std::uint32_t seed = 12345;
    };

    FafRuntime(const Config &config) : config_(config) {};
    FafRuntime(const FafRuntime &) = delete;

    auto SetSeed(const std::uint32_t seed) { config_.seed = seed; }

    auto Reset() {
        Init(config_.game_data_path);
        EnsureSimThread();

        ShutdownSim();

        auto config_state = LuaPlus::LuaState(LuaPlus::LuaState::LIB_BASE);

        if (!moho::SCR_LuaDoString(config_.lua_config_str.c_str(), &config_state)) {
            throw std::runtime_error("failed to evaluate Lua luanch config");
        }

        LuaPlus::LuaObject dat = config_state.GetGlobal("LaunchConfig");
        if (!dat.IsTable()) {
            throw std::runtime_error("Lua config must defined global 'LaunchConfig' table");
        }

        HeadlessRoster roster;
        auto build = moho::BuildLaunchInfo(dat, roster, config_.seed, false, &config_state);
        if (build.failure != nullptr || build.launchInfo.get() == nullptr) {
            throw std::runtime_error(
                std::format("moho::BuildLaunchInfo failed: {}",
                            build.failure != nullptr ? build.failure : "null"));
        }

        const auto loader = moho::GetWldSessionLoader();
        if (loader == nullptr) {
            throw std::runtime_error("moho::GetWldSessionLoader is null");
        }
        const auto scenario =
            loader->CreateScenarioInfo(build.mapName.c_str(), &build.launchInfo->mGameMods);
        while (!loader->IsLoaded()) {
            loader->Update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (scenario == nullptr || scenario->mGameRules == nullptr ||
            scenario->mWldMap == nullptr || scenario->mState == nullptr) {
            throw std::runtime_error(
                std::format("WldSessionLoader failed to load map: {}", build.mapName.c_str()));
        }

        moho::FAF_BeginSession(false);

        build.launchInfo->mGameRules = scenario->mGameRules;
        build.launchInfo->mMap = new moho::STIMap(scenario->mWldMap->mTerrainRes->mMap);
        build.launchInfo->mProps = scenario->mWldMap->mProps;
        const auto lang = scenario->mState->GetGlobal("__language").ToString();
        build.launchInfo->mLanguage.assign_owned(lang != nullptr ? lang : "us");

        sim_.reset(moho::Sim::Create(boost::SharedPtrRawFromSharedBorrow(
            boost::static_pointer_cast<moho::LaunchInfoBase>(build.launchInfo))));

        const auto map_bounds = scenario->mWldMap->mTerrainRes->mMap->GetBounds3D();
        map_width_ = map_bounds.Max.x - map_bounds.Min.x;
        map_height_ = map_bounds.Max.z - map_bounds.Min.z;

        // Run beat-0 sync, extract heigh map, then run beat 1
        SyncSim();
        ExtractHeightmap();
        ExtractResourceObservation();
        ExtractBlueprintDisplayNames();
        next_cmd_serial_ = 1u;
        Step({}, 1);
    }

    auto Step(const std::vector<Command> &commands, const std::uint32_t num_beats = 1) -> void {
        if (!sim_) {
            throw std::runtime_error("Step called before Reset()");
        }
        EnsureSimThread();

        // Dispatch Command
        for (auto const &command : commands) {
            DispatchCommand(command);
        }
        sim_->SetCommandSource(kInvalidCommandSource);

        for (std::uint32_t b = 0; b < num_beats; ++b) {
            sim_->AdvanceBeat(1);
            SyncSim();
        }

        CollectObservations();
    }

  private:
    std::uint32_t AllocateNextCmdId(std::uint32_t src_id) {
        for (std::uint32_t probe = 0u; probe < kCmdSerialMask; ++probe) {
            if ((next_cmd_serial_ & kCmdSerialMask) == 0u) {
                ++next_cmd_serial_;
            }
            const auto serial = (next_cmd_serial_++ & kCmdSerialMask);
            const auto candidate = static_cast<moho::CmdId>((src_id << 24u) | serial);
            if (sim_ == nullptr || sim_->mCommandDB == nullptr) {
                return static_cast<std::uint32_t>(candidate);
            }
            const auto it = sim_->mCommandDB->commands.find(candidate);
            if (it == sim_->mCommandDB->commands.end() || it->second == nullptr) {
                return static_cast<std::uint32_t>(candidate);
            }
        }

        throw std::runtime_error("24-bit command id pool exhausted");
    }

    bool IsRallyPointSplitCommand(const CommandType command_Type) {
        switch (command_Type) {

        case CommandType::Move:
        case CommandType::FormMove:
        case CommandType::Patrol:
        case CommandType::FormPatrol:
        case CommandType::TransportLoadUnits:
        case CommandType::AggressiveMove:
        case CommandType::FormAgressiveMove:
            return true;
        default:
            return false;
        }
    }

    auto DispatchCommand(const Command &command) -> void {
        const auto army = sim_->mArmiesList[command.army_id];
        if (army == nullptr) {
            return;
        }

        const auto src_id = army->mVarDat.mValidCommandSources.GetNext(~0u);
        if (src_id == army->mVarDat.mValidCommandSources.Max()) {
            return;
        }

        CommandValidator sink(*sim_, *army, command.army_id);
        sink.SetCommandSource(static_cast<moho::CommandSourceId>(src_id));

        std::vector<moho::Unit *> live_units;
        for (auto const &uid : command.unit_ids) {
            const auto ent_bits = static_cast<std::uint32_t>(uid);
            if ((ent_bits) >> 28u != kUnitEntityFamilyNibble || sim_->mEntityDB == nullptr) {
                continue;
            }

            auto const entity = sim_->mEntityDB->FindEntityById(ent_bits);
            if (entity == nullptr) {
                continue;
            }

            if (!sim_->OkayToMessWith(entity)) {
                return;
            }

            auto const unit = entity->IsUnit();
            if (unit != nullptr && !unit->IsDead() && !unit->DestroyQueued() &&
                !unit->IsUnitState(moho::UNITSTATE_UnSelectable) &&
                !unit->IsInCategory("UNSELECTABLE")) {
                live_units.emplace_back(unit);
            }
        }

        switch (command.op) {
        case ActionOp::ProcessInfoPair:
            if (command.info_key.empty()) {
                return;
            }
            for (auto const unit : live_units) {
                sink.ProcessInfoPair(unit->id_, command.info_key.c_str(), command.info_val.c_str());
            }
            return;
        case ActionOp::IssueCommand:
        case ActionOp::IssueFactoryCommand:
            break;

        default:
            return;
        }

        if (live_units.empty()) {
            return;
        }

        if (command.command_type <= CommandType::None || command.command_type > CommandType::Dock) {
            return;
        }

        if (command.op == ActionOp::IssueCommand &&
            (command.command_type == CommandType::BuildSiloTactical ||
             command.command_type == CommandType::BuildSiloNuke)) {
            const auto silo_key = (command.command_type == CommandType::BuildSiloTactical)
                                      ? "SiloBuildTactical"
                                      : "SiloBuildNuke";
            const int count = std::max(1, static_cast<int>(command.build_count));
            for (auto const &unit : live_units) {
                for (int i = 0; i < count; ++i) {
                    sink.ProcessInfoPair(unit->id_, silo_key, "add");
                }
            }
            return;
        }

        if (command.command_type == CommandType::BuildFactory) {
            std::erase_if(live_units, [](const auto *u) { return !u->IsInCategory("FACTORY"); });
        } else if (command.command_type == CommandType::BuildMobile) {
            if (command.target_type != TargetType::Ground) {
                return;
            }
            std::erase_if(live_units, [](const auto *u) {
                return !u->IsInCategory("ENGINEER") && !u->IsInCategory("NEEDMOBILEBUILD") &&
                       !u->IsInCategory("POD");
            });
        }
        if (live_units.empty()) {
            return;
        }

        moho::SSTICommandIssueData issue(static_cast<moho::EUnitCommandType>(command.command_type));

        if (command.target_type == TargetType::Entity) {
            issue.mTarget.mType = moho::EAiTargetType::AITARGET_Entity;
            issue.mTarget.mEntityId = command.target_entity_id;
        } else if (command.target_type == TargetType::Ground) {
            issue.mTarget.mType = moho::EAiTargetType::AITARGET_Ground;
            issue.mTarget.mPos =
                Wm3::Vector3f{command.target_x, command.target_y, command.target_z};
        }

        if (command.orientation_rad != 0.0f && std::isfinite(command.orientation_rad)) {
            const Wm3::Vector3f heading_axis{0.0f, 1.0f, 0.0f};
            moho::EulerRollToQuat(&heading_axis, &issue.mOri, command.orientation_rad);
        }

        if (command.bp_ordinal > 0 && sim_->mRules != nullptr) {
            if (const auto bp = sim_->mRules->GetBlueprintFromOrdinal(command.bp_ordinal - 1);
                bp != nullptr) {
                issue.mBlueprint =
                    sim_->mRules->GetUnitBlueprint(moho::RResId{.name = bp->mBlueprintId});
            }
        }

        for (const auto &cell : command.cells) {
            issue.mCells.push_back(moho::SOCellPos{cell.x, cell.z});
        }

        if (command.build_count > 0) {
            issue.unk70 = command.build_count;
            issue.unk74 = command.build_count; // ??? why is this the same values?
        }

        // We won't allow executing scripts
        // if (command.command_type == CommandType::Script && !command.lua_args_code.empty() &&
        // sim_->mLuaState != nullptr) {

        // }

        const bool clear_queue = command.clear_queue != 0;

        if (command.op == ActionOp::IssueFactoryCommand) {
            moho::BVSet<moho::EntId, moho::EntIdUniverse> factory_set;
            for (auto const &unit : live_units) {
                factory_set.Add(unit->id_);
            }
            issue.nextCommandId = AllocateNextCmdId(src_id);
            sink.IssueFactoryCommand(factory_set, issue, clear_queue);
            return;
        }

        if (IsRallyPointSplitCommand(command.command_type)) {
            moho::BVSet<moho::EntId, moho::EntIdUniverse> regular_set;
            moho::BVSet<moho::EntId, moho::EntIdUniverse> rally_factory_set;
            for (auto const &unit : live_units) {
                if (unit->IsInCategory("RALLYPOINT")) {
                    rally_factory_set.Add(unit->id_);
                } else {
                    regular_set.Add(unit->id_);
                }
            }

            if (!regular_set.Empty()) {
                issue.nextCommandId = AllocateNextCmdId(src_id);
                sink.IssueCommand(regular_set, issue, clear_queue);
            }
            if (!rally_factory_set.Empty()) {
                moho::SSTICommandIssueData rally_issue(issue);
                rally_issue.nextCommandId = AllocateNextCmdId(src_id);
                sink.IssueFactoryCommand(rally_factory_set, rally_issue, clear_queue);
            }
            return;
        }
        moho::BVSet<moho::EntId, moho::EntIdUniverse> unit_set;
        for (auto const &unit : live_units) {
            unit_set.Add(unit->id_);
        }
        issue.nextCommandId = AllocateNextCmdId(src_id);
        sink.IssueCommand(unit_set, issue, clear_queue);
    }

    Config config_;
};

} // namespace faf::py