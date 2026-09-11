from conan import ConanFile
from conan.tools.scm import Git
from conan.tools.files import rmdir, copy
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

import os


class Pluginval(ConanFile):
    name = "pluginval"
    version = "1.0.3"
    channel = "release"
    user = "pluginval"
    author = "pluginval"
    homepage = "https://github.com/Tracktion/pluginval"
    url = "https://github.com/Tracktion/pluginval.git"
    description = "Cross platform plugin testing and validation tool"
    license = "https://github.com/Tracktion/pluginval/blob/master/LICENSE"

    settings = "os", "compiler", "arch", "build_type"
    package_type = "application"
    exports_sources = "*", "!.vscode", "!build"

    def layout(self):
        cmake_layout(self)

    def source(self):
        rmdir(self, "pluginval")
        Git(self).run(f"clone --recurse-submodules --depth 1 --branch v{self.version} {self.url}")

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.blocks.remove("apple_system")  # Because Conan forces x86_64 build(from settings)
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure(build_script_folder=os.path.join(str(self.source_folder), "pluginval"))
        cmake.build()

    def package(self):
        copy(self, "*", src=os.path.join(str(self.build_folder), "pluginval_artefacts", str(self.settings.build_type)), dst=os.path.join(str(self.package_folder), "bin"))

    def package_info(self):
        if self.settings.os == "Macos":
            self.buildenv_info.append_path("PATH", os.path.join(str(self.package_folder), "bin", "pluginval.app", "Contents", "MacOS"))
        else:
            self.buildenv_info.append_path("PATH", os.path.join(str(self.package_folder), "bin"))
