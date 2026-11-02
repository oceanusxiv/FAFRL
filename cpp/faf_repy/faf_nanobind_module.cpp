#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>

#include "faf_runtime.hpp"

namespace nb = nanobind;
using namespace nb::literals;

namespace faf::py {
struct Observation {
    nb::ndarray<nb::numpy, float, nb::ndim<2>> heightmap;
};

class FafEnv {
  public:
    FafEnv(const std::string &game_data_dir, const std::string &lua_config_str)
        : runtime_({game_data_dir, lua_config_str}) {};
    auto reset() { runtime_.Reset(); }

    auto step() -> Observation {
        runtime_.Step();

        auto heighmap_cpp = runtime_.GetHeightmap();

        nb::handle owner_handle = nb::cast(this);
        return {.heightmap = nb::ndarray<nb::numpy, float, nb::ndim<2>>(
                    heighmap_cpp.data_handle(), {heighmap_cpp.extent(0), heighmap_cpp.extent(1)},
                    owner_handle)};
    }

  private:
    FafRuntime runtime_;
};

NB_MODULE(faf_repy, m) {
    nb::class_<Observation>(m, "Observation")
        .def_ro("heightmap", &Observation::heightmap, nb::rv_policy::reference);

    nb::class_<FafEnv>(m, "FafEnv")
        .def(nb::init<const std::string &, const std::string &>())
        .def("reset", &FafEnv::reset)
        .def("step", &FafEnv::step);
}
} // namespace faf::py
