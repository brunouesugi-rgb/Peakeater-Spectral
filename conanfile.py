from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, CMake, cmake_layout
from conan.tools.env import VirtualBuildEnv


class PeakEaterSpectral(ConanFile):
    name = "peakeater_spectral"
    version = "0.1.0"
    user = "axlrtr-audio-lab"
    channel = "testing"
    company = "AXLRTR Audio Lab"
    product_name = "Peakeater Spectral Beta"
    url = "https://github.com/axlrtr-audio-lab/peakeater-spectral"
    settings = "os", "arch", "compiler", "build_type"
    exports_sources = "assets", "modules", "source", "CMakeLists.txt"
    package_type = "application"

    requires = "juce/7.0.5@juce/release"
    # The original project builds pluginval through Conan for CI. On this
    # Windows/MSVC toolchain pluginval 1.0.3 currently fails to compile, and we
    # already keep a working pluginval binary in ../tools/pluginval for local
    # validation.

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.blocks.remove("apple_system")  # Because Conan forces x86_64 build(from settings)
        toolchain.cache_variables["CMAKE_PROJECT_NAME"] = str(self.name)
        toolchain.cache_variables["CMAKE_PROJECT_VERSION"] = str(self.version)
        toolchain.cache_variables["CONAN_PROJECT_COMPANY"] = str(self.company)
        toolchain.cache_variables["CONAN_PROJECT_PRODUCT_NAME"] = str(self.product_name)
        toolchain.cache_variables["CONAN_PROJECT_URL"] = str(self.url)
        toolchain.generate()
        dependencies = CMakeDeps(self)
        dependencies.generate()
        buildenv = VirtualBuildEnv(self)
        buildenv.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
