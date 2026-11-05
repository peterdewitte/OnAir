#!/usr/bin/env python3
"""Lists the tests that are worth running on Windows and macOS on every push, for tests/platform_tests.txt.

Most tests check decoders and DSP: the same C++ on every OS, already run on Linux on every push. Running them
again on Windows and macOS mostly costs time (about 1h40 of tests and 2h20 of building on the Windows runner).
Two kinds of test do differ per OS, and these are listed:

  platform  the test or the code it covers talks to the OS: sockets, files and folders, settings and data paths,
            loading libraries and plug-ins, processes, audio, GUI/OpenGL, local time, #ifdef _WIN32 / __APPLE__
            code; and every socket/thread timing test (label "timing")
  kernels   SIMD / FFT / FEC kernels: another instruction set (NEON on arm64), Accelerate on macOS, the Windows
            x64 calling convention (stack alignment of the AVX2 code, #324)

Every other test still builds and runs on Windows and macOS in the nightly Full CI (ci-full.yml).

Run after adding tests (from a configured build directory, the tests need not be built):
    python3 tools/platform_tests.py build > tests/platform_tests.txt
Each line: <label> <test> <build target>  # why
"""
import functools
import glob
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# tests/<area>/ whose subject is an OS integration: every test in it is listed
PLATFORM_AREAS = {
    "source", "net", "server", "audio", "codec", "plugin", "session", "engine", "backup", "power", "mcp",
    "webshare", "packs", "time", "export", "results", "kit", "transcribe", "integrations", "presets", "tools", "web",
}
# tests/<area>/ of the numeric kernels, and test names that point at one
KERNEL_AREAS = {"dsp", "fec", "t2"}
KERNEL_NAME = re.compile(r"viterbi|fft|simd|ldpc|resampl|neon|avx|polyphase|fir_|kernel")
# tests that CMake only adds on macOS or Windows, so a Linux build directory does not list them: <test>: (<target>, why)
NOT_ON_LINUX = {
    "memory_recall_app": ("dect2", "a hidden window run of the app, macOS and Windows only"),
}

# code in the test's own source that behaves differently per OS (a temporary file through std::filesystem does
# not count: that is portable and every test may write one)
MARKERS = {
    "os_ifdef": re.compile(r"#\s*(?:if|ifdef|ifndef|elif).*\b(?:_WIN32|_WIN64|__APPLE__|__MINGW32__|_MSC_VER|__linux__|TARGET_OS_\w+)\b"),
    "net": re.compile(r"<(?:sys/socket|winsock2|ws2tcpip|netinet/\w+|arpa/inet|netdb)\.h>|\b(?:recvfrom|sendto|getaddrinfo|WSAStartup)\s*\("),
    "dynlib": re.compile(r"<dlfcn\.h>|\b(?:dlopen|LoadLibrary\w*|GetProcAddress)\b"),
    "process": re.compile(r"\b(?:fork|execvp?|posix_spawn\w*|CreateProcess\w*|popen|_popen|_wpopen)\s*\("),
    "text_fopen": re.compile(r"\bfopen\s*\([^;\n]{0,200},\s*\"[rwa]\+?\"\s*\)"),   # CR/LF translation on Windows
    "audio_dev": re.compile(r"\b(?:ma_device_init|waveOut\w*|AudioQueue\w*|AudioUnit\w*|snd_pcm_open|pa_simple_new)\b"),
    "serial": re.compile(r"<termios\.h>|\btcsetattr\b"),
    "env_paths": re.compile(r"\b(?:getenv|_wgetenv)\s*\(\s*\"(?:HOME|APPDATA|LOCALAPPDATA|XDG_\w+|USERPROFILE|TMPDIR|TEMP|TMP)\""),
    "local_time": re.compile(r"\b(?:timegm|_mkgmtime|localtime_r|localtime_s|gmtime_r|gmtime_s|mktime|strftime|tzset)\b"),
    "gui": re.compile(r"<GLFW/|\bglfw[A-Z]\w*\(|<OpenGL/|<GL/|<Metal/|\"imgui\.h\""),
    "accelerate": re.compile(r"<Accelerate/|\bvDSP_\w+"),
}


@functools.lru_cache(None)
def markers(path):
    try:
        text = open(path, errors="replace").read()
    except OSError:
        return ()
    return tuple(k for k, rx in MARKERS.items() if rx.search(text))


def cmake_targets():
    """test name -> build target, and the set of executable targets, from the add_test/add_executable lines."""
    cm = open(os.path.join(ROOT, "CMakeLists.txt")).read()
    exes = set(re.findall(r"add_executable\(([A-Za-z0-9_\-]+)", cm))
    by_test = {}
    for name, rest in re.findall(r"add_test\(NAME ([A-Za-z0-9_]+) COMMAND ([^\n]*)", cm):
        cmd = rest.split()[0].rstrip(")")
        if not cmd.startswith("$"):
            by_test[name] = cmd
        else:   # run through cmake -E env / python / node: the program it starts
            m = re.search(r"\$<TARGET_FILE:([A-Za-z0-9_\-]+)>", rest)
            by_test[name] = m.group(1) if m else "-"
    return by_test, exes


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    out = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"], capture_output=True, text=True, check=True).stdout
    tests = json.loads(out)["tests"]
    by_test, exes = cmake_targets()
    sources = {os.path.splitext(os.path.basename(f))[0]: f for f in glob.glob(os.path.join(ROOT, "tests", "**", "*.cpp"), recursive=True)}

    rows = []
    for t in tests:
        name = t["name"]
        labels = next((p["value"] for p in t.get("properties", []) if p["name"] == "LABELS"), [])
        if "slow" in labels:   # the slow tier runs nightly on every OS anyway
            continue
        target = by_test.get(name) or ("test_" + name if "test_" + name in exes or "test_" + name in sources else name)
        src = sources.get(target) or sources.get("test_" + name)
        area = os.path.relpath(src, ROOT).split(os.sep)[1] if src else ""
        why = None
        if area in PLATFORM_AREAS:
            why = "platform", "area " + area
        elif "timing" in labels:
            why = "platform", "timing"
        else:
            found = markers(src) if src else ()
            if found:
                why = "platform", ", ".join(found)
            elif area in KERNEL_AREAS or KERNEL_NAME.search(name):
                why = "kernels", "area " + area if area in KERNEL_AREAS else "kernel name"
        if why:
            rows.append((why[0], name, target, why[1]))
    listed = {r[1] for r in rows}
    for name, (target, why) in NOT_ON_LINUX.items():
        if name not in listed:
            rows.append(("platform", name, target, why))

    print("# Tests that run on Windows and macOS on every push (ci.yml, ctest -L \"platform|kernels\"): the ones whose")
    print("# code differs per OS. Linux runs every test on every push, Windows and macOS run the rest nightly (ci-full.yml).")
    print("# Regenerate after adding tests: python3 tools/platform_tests.py build > tests/platform_tests.txt")
    print("# <label> <test> <build target>  # why")
    for label, name, target, why in sorted(rows):
        print(f"{label} {name} {target}  # {why}")
    n_fast = sum(1 for t in tests if "slow" not in next((p["value"] for p in t.get("properties", []) if p["name"] == "LABELS"), []))
    print(f"{len(rows)} of {n_fast} quick tests listed", file=sys.stderr)


if __name__ == "__main__":
    main()
