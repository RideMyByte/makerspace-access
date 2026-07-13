import os
import shutil

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()
pkg_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
src = os.path.join(pkg_dir, "tools", "platformio-build-esp32.py")
dst = os.path.join(pkg_dir, "tools", "platformio-build-esp32c6.py")

if not os.path.exists(dst) and os.path.exists(src):
    shutil.copy2(src, dst)
    print(f"Created missing build script: {dst}")
