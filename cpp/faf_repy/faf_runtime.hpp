#pragma once

#include "gpg/core/utils/Logging.h"
#include "legacy/math/X87Math.h"
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
#include "moho/sim/CWldSessionLoaderImpl.h"
#include "moho/sim/FafGameVersion.h"
#include "moho/sim/RRuleGameRules.h"
#include "moho/sim/Sim.h"
#include "moho/unit/core/IUnit.h"
#include "remote/InboundIds.h"
#include "remote/VisibilityOracle.h"
#include <format>
#include <mdspan>
#include <memory>
#include <print>
#include <string>
#include <thread>

namespace faf::py {

struct UnitObservation {
    std::int32_t entity_id;
    std::uint16_t bp_ordinal;
    std::uint8_t army_index;
    std::uint8_t unit_flags;
    float pos_x;
    float pos_y; // Elevation
    float pos_z;
    float vel_x;
    float vel_y;
    float vel_z;
    float health;
    float max_health;
    float fraction_complete;
    float shield_ratio;
    float fuel_ratio;
    float work_progress;
    std::uint32_t command_queue_len;
    std::uint32_t script_bits;
    std::uint64_t unit_state_mask;
};
static_assert(sizeof(UnitObservation) == 72, "Must be tightly packed");

struct EconomyObservation {
    float mass_stored;
    float energy_stored;
    float mass_income;
    float energy_income;
    float mass_requested;
    float energy_requested;
    float mass_actual;
    float energy_actual;
    float mass_max_storage;
    float energy_max_storage;
    std::uint8_t is_out_of_game;
    std::uint8_t pad[3];
};
static_assert(sizeof(EconomyObservation) == 44, "Must be tightly packed");

struct ResourceObservation {
    std::uint8_t deposit_type; // EDepositType (1 = Mass, 2 = Hydrocarbon)
    std::uint8_t pad[3];
    std::int16_t x0;
    std::int16_t z0;
    std::int16_t x1;
    std::int16_t z1;
    float center_x;
    float center_y;
    float center_z;
};
static_assert(sizeof(ResourceObservation) == 24, "Must be tightly packed");

struct ReclaimableObservation {
    std::int32_t entity_id;
    std::uint16_t bp_ordinal;
    std::uint8_t is_wreckage;
    std::uint8_t pad;
    float pos_x;
    float pos_y;
    float pos_z;
    float mass;
    float energy;
    float max_mass;
    float mass_energy;
    float reclaim_fraction;
};
static_assert(sizeof(ResourceObservation) == 40, "Must be tightly packed");

// Mirrors moho::EUnitCommandType (moho/command/SSTICommandIssueData.h).
enum class CommandType : std::uint8_t {
    None = 0x00,
    Stop = 0x01,
    Move = 0x02,
    Dive = 0x03,
    FormMove = 0x04,
    BuildSiloTactical = 0x05,
    BuildSiloNuke = 0x06,
    BuildFactory = 0x07,
    BuildMobile = 0x08,
    BuildAssist = 0x09,
    Attack = 0x0A,
    FormAttack = 0x0B,
    Nuke = 0x0C,
    Tactical = 0x0D,
    Teleport = 0x0E,
    Guard = 0x0F,
    Patrol = 0x10,
    Ferry = 0x11,
    FormPatrol = 0x12,
    Reclaim = 0x13,
    Repair = 0x14,
    Capture = 0x15,
    TransportLoadUnits = 0x16,
    TransportReverseLoadUnits = 0x17,
    TransportUnloadUnits = 0x18,
    TransportUnloadSpecificUnits = 0x19,
    DetachFromTransport = 0x1A,
    Upgrade = 0x1B,
    Script = 0x1C,
    AssistCommander = 0x1D,
    KillSelf = 0x1E,
    DestroySelf = 0x1F,
    Sacrifice = 0x20,
    Pause = 0x21,
    Overcharge = 0x22,
    AggressiveMove = 0x23,
    FormAgressiveMove = 0x24,
    AssistMove = 0x25,
    SpecialAction = 0x26,
    Dock = 0x27,
};

enum class TargetType : std::uint8_t {
    None = 0,
    Entity = 1,
    Ground = 2,
};

enum class ActionOp : std::uint8_t {
    IssueCommand = 0,
    IssueFactoryCommand = 1,
    ProcessInfoPair = 2,
};

struct CellPos {
    std::int16_t x = 0;
    std::int16_t z = 0;
};
static_assert(sizeof(CellPos) == 4, "Not tightly packed");

namespace UnitObservationFlags {
inline constexpr std::uint8_t kPaused = 1u << 0;
inline constexpr std::uint8_t kAutoMode = 1u << 1;
inline constexpr std::uint8_t kAutoSurfaceMode = 1u << 2;
inline constexpr std::uint8_t kBusy = 1u << 3;
inline constexpr std::uint8_t kRepeatQueue = 1u << 4;
inline constexpr std::uint8_t kValidTarget = 1u << 5;
inline constexpr std::uint8_t kOverchargePaused = 1u << 6;
inline constexpr std::uint8_t kBeingBuilt = 1u << 7;
} // namespace UnitObservationFlags

struct Command {
    std::uint8_t army_id = 0;
    ActionOp op = ActionOp::IssueCommand;
    CommandType command_type = CommandType::None;
    TargetType target_type = TargetType::None;
    std::uint8_t clear_queue = 0;
    std::int32_t target_entity_id = -1;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float target_z = 0.0f;
    float orientation_rad = 0.0f;
    std::uint16_t bp_ordinal = 0;
    std::int16_t build_count = 1;
    std::string info_key;
    std::string info_val;
    std::string lua_args_code;
    std::vector<CellPos> cells;
    std::vector<std::int32_t> unit_ids;
};

struct ScenarioAssetBundle {
    moho::SWldGameData game_data{};

    ScenarioAssetBundle() = default;
    ScenarioAssetBundle(const ScenarioAssetBundle &) = delete;
    ScenarioAssetBundle &operator=(const ScenarioAssetBundle &) = delete;

    ~ScenarioAssetBundle() { moho::ReleaseWldGameDataHandles(&game_data); }
};

namespace {
constexpr moho::CommandSourceId kInvalidCommandSource = 0xFFu;
constexpr std::uint32_t kCmdSerialMask = 0x00FFFFFFu;
constexpr std::uint32_t kUnitEntityFamilyNibble = 0u;

auto DieHandler(const char *const message) {
    std::println(stderr, "Fatal engine error: {}", message != nullptr ? message : "null");
}

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
    return {.entity_id = static_cast<std::int32_t>(unit.id_),
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
        if (sim_.mEntityDB == nullptr) {
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

class FafRuntime {
  public:
    struct Config {
        std::string game_data_path; // Directory containing init.lua.
        std::string lua_config_str;
        std::uint32_t seed = 12345;
    };

    FafRuntime(const Config &config) : config_(config) {};
    FafRuntime(const FafRuntime &) = delete;
    ~FafRuntime() { ShutdownSim(); }

    auto Reset() {
        Init(config_.game_data_path);
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

        moho::tIsSimThread = true;
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

    auto GetHeightmap() { return std::mdspan(heightmap_.data(), map_width_, map_height_); }
    auto GetUnitObservation() const -> const std::vector<UnitObservation> & {
        return unit_obs_buffer_;
    }
    auto GetUnitObservationForArmy(int army_index) const -> const std::vector<UnitObservation> & {
        return {};
    }
    auto GetEconomyObservation() const -> const std::vector<EconomyObservation> & {
        return econ_obs_buffer_;
    }
    auto GetResourceObservation() const -> const std::vector<ResourceObservation> & {
        return resource_obs_buffer_;
    }
    auto GetReclaimableObservation() const -> const std::vector<ReclaimableObservation> & {
        return reclaim_obs_buffer_;
    }
    auto GetReclaimableObservationForArmy(int army_index) const
        -> const std::vector<ReclaimableObservation> & {
        return {};
    }
    auto GetBlueprintDisplayNames() const -> const std::vector<std::string> & { return bp_names_; }
    auto IsGameOver() const { return sim_ ? (sim_->mGameEnded || sim_->mGameOver) : false; }
    auto GetCurrentBeat() const { return sim_ ? (sim_->mCurBeat != 0) : 0u; }

  private:
    auto Init(const std::string &game_data_path) -> void {
        static std::once_flag init_once;
        std::call_once(init_once, [&]() {
            gpg::SetDieHandler(&DieHandler);
            static gpg::StreamLogTarget stderr_log(std::cerr, 1u);

            (void)SetSse2Mode(moho::CFG_GetArgOption("/sse2", 0, nullptr) ? 1 : 0);

            gpg::REF_RegisterAllTypes();
            moho::RES_EnsureResourceManager();
            moho::RES_ActivatePendingFactories();

            const msvc8::string data_path_script_name("SupComDataPathFAFRL.lua");
            if (!moho::DISK_SetupDataAndSearchPaths(data_path_script_name, game_data_path)) {
                throw std::runtime_error(
                    std::format("FafRuntime::Init: moho::DISK_SetupDataAndSearchPaths "
                                "failed for {} and {}",
                                game_data_path, data_path_script_name.c_str()));
            }
        });
    }

    auto ShutdownSim() -> void {
        heightmap_.clear();
        unit_obs_buffer_.clear();
        econ_obs_buffer_.clear();

        if (!sim_) {
            return;
        }

        if (sim_) {
            sim_->Shutdown();
            SyncSim();
            sim_.reset();
        }
    }

    auto SyncSim() -> void {
        moho::SSyncData *sync = nullptr;
        sim_->Sync(sim_->mSyncFilter, sync);
        delete sync;
    }

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

    auto CollectObservations() -> void {
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

    bool IsRallyPointSplitCommand(const CommandType command_Type) {
        switch (command_Type) {

        case CommandType::None:
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

        if (!IsRallyPointSplitCommand(command.command_type)) {
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
                issue.nextCommandId = AllocateNextCmdId(src_id);
                sink.IssueFactoryCommand(rally_factory_set, rally_issue, clear_queue);
            }
        }
        moho::BVSet<moho::EntId, moho::EntIdUniverse> unit_set;
        for (auto const &unit : live_units) {
            unit_set.Add(unit->id_);
        }
        issue.nextCommandId = AllocateNextCmdId(src_id);
        sink.IssueCommand(unit_set, issue, clear_queue);
    }

    auto ExtractHeightmap() -> void {
        if (!sim_) {
            return;
        }

        const auto hf = sim_->mMapData->GetHeightField();
        if (!hf) {
            return;
        }

        map_width_ = hf->Width();
        map_height_ = hf->Height();
        heightmap_.resize(map_width_ * map_height_);

        // GetHeightAt returns uint16_t at 1/128 scale
        constexpr float kHeightWorldScale = 1.0f / 128.0f;
        for (size_t z = 0; z < map_height_; ++z) {
            for (size_t x = 0; x < map_width_; ++x) {
                heightmap_[z * map_width_ + x] = hf->GetHeightAt(x, z) * kHeightWorldScale;
            }
        }
    }

    auto ExtractBlueprintDisplayNames() -> void {
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

    auto ExtractResourceObservation() -> void {
        resource_obs_buffer_.clear();
        if (!sim_) {
            return;
        }
        const auto resources = sim_->GetResources();
        if (resources == nullptr) {
            return;
        }

        const auto hf = sim_->mMapData->GetHeightField();
        const auto &deposits = resources->GetDeposits();
        resource_obs_buffer_.reserve(deposits.size());
        for (auto const &deposit : deposits) {
            if (deposit.depositType != moho::EDepositType::kNone) {
                const float center_x = (deposit.footprintRect.x0 + deposit.footprintRect.x1) * 0.5f;
                const float center_z = (deposit.footprintRect.z0 + deposit.footprintRect.z1) * 0.5f;
                const float center_y = hf != nullptr ? hf->GetElevation(center_x, center_z) : 0.0f;
                resource_obs_buffer_.emplace_back(ResourceObservation{
                    .deposit_type = deposit.depositType,
                    .pad = {0u, 0u, 0u},
                    .x0 = static_cast<std::int16_t>(deposit.footprintRect.x0),
                    .z0 = static_cast<std::int16_t>(deposit.footprintRect.z0),
                    .x1 = static_cast<std::int16_t>(deposit.footprintRect.x1),
                    .z1 = static_cast<std::int16_t>(deposit.footprintRect.z1),
                    .center_x = center_x,
                    .center_y = center_y,
                    .center_z = center_z,
                });
            }
        }
    }

    Config config_;
    std::shared_ptr<ScenarioAssetBundle> scenario_bundle_;
    std::unique_ptr<moho::Sim> sim_;
    std::vector<UnitObservation> unit_obs_buffer_;
    std::vector<EconomyObservation> econ_obs_buffer_;
    std::vector<ResourceObservation> resource_obs_buffer_;
    std::vector<ReclaimableObservation> reclaim_obs_buffer_;
    std::uint32_t next_cmd_serial_ = 1u;

    std::vector<float> heightmap_;
    std::vector<std::string> bp_names_;
    size_t map_width_ = 0;
    size_t map_height_ = 0;
};

} // namespace faf::py
