#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include "faf_runtime.hpp"

namespace nb = nanobind;
using namespace nb::literals;

namespace faf::py {

constexpr std::size_t kUnitObsLen = 10;
constexpr std::size_t kEconObsLen = 11;
constexpr std::size_t kResObsLen = 4;

class FafEnv {
  public:
    struct Observation {
        nb::ndarray<nb::numpy, float, nb::ndim<2>> heightmap;
        nb::ndarray<nb::numpy, float, nb::shape<-1, kEconObsLen>> economy;
        nb::ndarray<nb::numpy, float, nb::shape<-1, kResObsLen>> resources;
        nb::ndarray<nb::numpy, float, nb::shape<-1, kUnitObsLen>> units;
    };

    FafEnv(const std::string &game_data_dir, const std::string &lua_config_str,
           bool include_unit_name)
        : runtime_({game_data_dir, lua_config_str}) {};

    auto GetAllDisplayNames() const -> const std::vector<std::string> & {
        return runtime_.GetBlueprintDisplayNames();
    }

    auto IsGameOver() { return runtime_.IsGameOver(); }

    auto Reset() { runtime_.Reset(); }

    auto Step(const std::vector<Command> &commands) -> Observation {
        runtime_.Step(commands);

        auto heightmap_cpp = runtime_.GetHeightmap();

        const auto &units = runtime_.GetUnitObservation();
        auto unit_vec = std::make_unique<std::vector<float>>();
        unit_vec->reserve(units.size() * kUnitObsLen);
        for (const auto &unit : units) {
            unit_vec->emplace_back(static_cast<float>(unit.pos_x));
            unit_vec->emplace_back(static_cast<float>(unit.pos_y));
            unit_vec->emplace_back(static_cast<float>(unit.pos_z));
            unit_vec->emplace_back(static_cast<float>(unit.vel_x));
            unit_vec->emplace_back(static_cast<float>(unit.vel_y));
            unit_vec->emplace_back(static_cast<float>(unit.vel_z));
            unit_vec->emplace_back(static_cast<float>(unit.army_index));
            unit_vec->emplace_back(static_cast<float>(unit.health));
            unit_vec->emplace_back(static_cast<float>(unit.max_health));
            unit_vec->emplace_back(static_cast<float>(unit.bp_ordinal));
        }
        float *unit_raw_ptr = unit_vec->data();
        auto unit_owner = nb::capsule(unit_vec.release(), [](void *p) noexcept {
            delete static_cast<std::vector<float> *>(p);
        });

        const auto &economy = runtime_.GetEconomyObservation();
        auto econ_vec = std::make_unique<std::vector<float>>();
        econ_vec->reserve(economy.size() * kEconObsLen);
        for (const auto &econ : economy) {
            econ_vec->emplace_back(static_cast<float>(econ.mass_stored));
            econ_vec->emplace_back(static_cast<float>(econ.energy_stored));
            econ_vec->emplace_back(static_cast<float>(econ.mass_income));
            econ_vec->emplace_back(static_cast<float>(econ.energy_income));
            econ_vec->emplace_back(static_cast<float>(econ.mass_requested));
            econ_vec->emplace_back(static_cast<float>(econ.energy_requested));
            econ_vec->emplace_back(static_cast<float>(econ.mass_actual));
            econ_vec->emplace_back(static_cast<float>(econ.energy_actual));
            econ_vec->emplace_back(static_cast<float>(econ.mass_max_storage));
            econ_vec->emplace_back(static_cast<float>(econ.energy_max_storage));
            econ_vec->emplace_back(static_cast<float>(econ.is_out_of_game));
        }

        float *econ_raw_ptr = econ_vec->data();
        auto econ_owner = nb::capsule(econ_vec.release(), [](void *p) noexcept {
            delete static_cast<std::vector<float> *>(p);
        });

        const auto &resources = runtime_.GetResourceObservation();
        auto res_vec = std::make_unique<std::vector<float>>();
        res_vec->reserve(resources.size() * kResObsLen);
        for (const auto &res : resources) {
            res_vec->emplace_back(static_cast<float>(res.center_x));
            res_vec->emplace_back(static_cast<float>(res.center_y));
            res_vec->emplace_back(static_cast<float>(res.center_z));
            res_vec->emplace_back(static_cast<float>(res.deposit_type));
        }
        float *res_raw_ptr = res_vec->data();
        auto res_owner = nb::capsule(res_vec.release(), [](void *p) noexcept {
            delete static_cast<std::vector<float> *>(p);
        });

        nb::handle hm_owner = nb::cast(this);
        return {.heightmap = {heightmap_cpp.data_handle(),
                              {heightmap_cpp.extent(0), heightmap_cpp.extent(1)},
                              hm_owner},
                .economy = {econ_raw_ptr, {economy.size(), kEconObsLen}, econ_owner},
                .resources = {res_raw_ptr, {resources.size(), kResObsLen}, res_owner},
                .units = {unit_raw_ptr, {units.size(), kUnitObsLen}, unit_owner}};
    }

  private:
    FafRuntime runtime_;
};

NB_MODULE(faf_repy, m) {
    nb::class_<Command>(m, "Command")
        .def(nb::init<>())
        .def_rw("army_id", &Command::army_id)
        .def_rw("op", &Command::op)
        .def_rw("command_type", &Command::command_type)
        .def_rw("target_type", &Command::target_type)
        .def_rw("clear_queue", &Command::clear_queue)
        .def_rw("target_entity_id", &Command::target_entity_id)
        .def_rw("target_x", &Command::target_x)
        .def_rw("target_y", &Command::target_y)
        .def_rw("target_z", &Command::target_z)
        .def_rw("orientation_rad", &Command::orientation_rad)
        .def_rw("bp_ordinal", &Command::bp_ordinal)
        .def_rw("build_count", &Command::build_count)
        .def_rw("info_key", &Command::info_key)
        .def_rw("info_val", &Command::info_val)
        .def_rw("lua_args_code", &Command::lua_args_code)
        .def_rw("cells", &Command::cells)
        .def_rw("unit_ids", &Command::unit_ids);

    nb::class_<FafEnv::Observation>(m, "Observation")
        .def_ro("heightmap", &FafEnv::Observation::heightmap, nb::rv_policy::reference)
        .def_ro("economy", &FafEnv::Observation::economy, nb::rv_policy::reference)
        .def_ro("resources", &FafEnv::Observation::resources, nb::rv_policy::reference)
        .def_ro("units", &FafEnv::Observation::units, nb::rv_policy::reference);

    nb::class_<FafEnv>(m, "FafEnv")
        .def(nb::init<const std::string &, const std::string &, bool>(), "game_data_dir"_a,
             "lua_config_str"_a, "include_unit_name"_a = false)
        .def("reset", &FafEnv::Reset)
        .def("step", &FafEnv::Step)
        .def("is_game_over", &FafEnv::IsGameOver)
        .def("get_all_display_names", &FafEnv::GetAllDisplayNames);
}
} // namespace faf::py
