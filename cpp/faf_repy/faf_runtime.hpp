#pragma once

#include "gpg/core/utils/Logging.h"
#include "legacy/math/X87Math.h"
#include "moho/misc/LaunchInfoBase.h"
#include "moho/misc/StartupHelpers.h"
#include "moho/net/LaunchInfoBuilder.h"
#include "moho/resource/ResourceManager.h"
#include "moho/sim/CWldMap.h"
#include "moho/sim/CWldSessionLoaderImpl.h"
#include "moho/sim/FafGameVersion.h"
#include "moho/sim/Sim.h"

#include <format>
#include <mdspan>
#include <memory>
#include <print>
#include <string>
#include <thread>

namespace faf::py {

namespace {
constexpr moho::CommandSourceId kInvalidCommandSource = 0xFFu;

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
        next_cmd_serial_ = 1u;
        Step();
    }

    auto SyncSim() -> void {
        moho::SSyncData *sync = nullptr;
        sim_->Sync(sim_->mSyncFilter, sync);
        delete sync;
    }

    auto Step(const std::uint32_t num_beats = 1) -> void {
        if (!sim_) {
            throw std::runtime_error("Step called before Reset()");
        }

        // Dispatch Command

        sim_->SetCommandSource(kInvalidCommandSource);

        for (std::uint32_t b = 0; b < num_beats; ++b) {
            sim_->AdvanceBeat(1);
            SyncSim();
        }
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

    auto GetHeightmap() { return std::mdspan(heightmap_.data(), map_width_, map_height_); }

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

            moho::FAF_BeginSession(false);
        });
    }

    auto ShutdownSim() -> void {
        if (!sim_) {
            return;
        }

        if (sim_) {
            sim_->Shutdown();
            SyncSim();
            sim_.reset();
        }
    }

    Config config_;
    std::unique_ptr<moho::Sim> sim_;
    std::uint32_t next_cmd_serial_ = 1u;

    std::vector<float> heightmap_;
    size_t map_width_ = 0;
    size_t map_height_ = 0;
};

} // namespace faf::py
