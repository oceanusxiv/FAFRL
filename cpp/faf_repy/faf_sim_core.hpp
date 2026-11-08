#pragma once

#include <mdspan>
#include <memory>
#include <string>

#include "moho/command/CCommandDb.h"
#include "moho/entity/EntityDb.h"
#include "moho/misc/LaunchInfoBase.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/sim/CArmyImpl.h"
#include "moho/sim/CWldMap.h"
#include "moho/sim/Sim.h"
#include "moho/sim/WldSessionInfo.h"
#include "moho/unit/core/IUnit.h"
#include "remote/VisibilityOracle.h"

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
    float max_energy;
    float reclaim_fraction;
};
static_assert(sizeof(ReclaimableObservation) == 40, "Must be tightly packed");

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
    LuaSimCallback = 3,
    DecreaseCommandCount = 4,
    RemoveCommandFromQueue = 5,
    IncreaseCommandCount = 6,
    SetCommantTarget = 7,
    SetCommandType = 8,
    SetCommandCells = 9,
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

class FafSimCore {
  public:
    FafSimCore(const FafSimCore &) = delete;

    auto GetHeightmap() { return std::mdspan(heightmap_.data(), map_height_, map_width_); }
    auto GetUnitObservation() const -> const std::vector<UnitObservation> & {
        return unit_obs_buffer_;
    }
    auto GetUnitObservationForArmy(int army_index) const -> const std::vector<UnitObservation> {
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
        -> const std::vector<ReclaimableObservation> {
        return {};
    }
    auto GetBlueprintDisplayNames() const -> const std::vector<std::string> & { return bp_names_; }
    auto IsGameOver() const { return sim_ ? (sim_->mGameEnded || sim_->mGameOver) : false; }
    auto GetCurrentBeat() const { return sim_ ? sim_->mCurBeat : 0u; }

  protected:
    FafSimCore() = default;
    ~FafSimCore() { ShutdownSim(); }

    auto Init(const std::string &game_data_path) -> void;

    auto ShutdownSim() -> void;

    auto SyncSim() -> void {
        moho::SSyncData *sync = nullptr;
        sim_->Sync(sim_->mSyncFilter, sync);
        delete sync;
    }

    auto CollectObservations() -> void;

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

    auto ExtractBlueprintDisplayNames() -> void;

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
