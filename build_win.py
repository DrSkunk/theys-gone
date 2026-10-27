import os
import subprocess
import glob
import sys

os.chdir(r"C:\Users\SlouMa\Desktop\days gone\theys-gone")
os.makedirs("package", exist_ok=True)

source = r".deps/imgui-1.91.4"
cpp_files = glob.glob(r"src/coop/*.cpp")
imgui_files = [
    os.path.join(source, "imgui.cpp"),
    os.path.join(source, "imgui_draw.cpp"),
    os.path.join(source, "imgui_tables.cpp"),
    os.path.join(source, "imgui_widgets.cpp"),
    os.path.join(source, "backends", "imgui_impl_dx11.cpp"),
]

cmd = [
    "g++",
    "-std=c++20",
    "-O2",
    "-Wall",
    "-Wextra",
    "-shared",
    "-static",
    "-s",
    '-DCOOP_VERSION="0.4.0-spacewar"',
    f"-I{source}",
    "-o", "package/TheysGone.asi",
    *cpp_files,
    *imgui_files,
    "-lws2_32",
    "-lbcrypt",
    "-ld3d11",
    "-ld3dcompiler",
    "-ldxgi",
    "-Wl,--subsystem,windows",
]

print("Compiling TheysGone.asi with g++...")
res = subprocess.run(cmd, capture_output=True, text=True)
if res.returncode != 0:
    print("Compilation FAILED:")
    print(res.stderr)
    sys.exit(1)
else:
    print("Compilation SUCCEEDED!")
    if res.stderr:
        print("Warnings:", res.stderr)
    print("Output file size:", os.path.getsize("package/TheysGone.asi"))
