"""Run production AFE output functions against deterministic host I/O stubs.

Requires a C++17 compiler. This checks delivery/ownership contracts, not DSP
quality or FreeRTOS timing; those require firmware and physical-device tests.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def function_body(source, signature):
    start = source.index(signature)
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor]

def test_ready_output_does_not_wait_for_input():
    source = (ROOT / "src/esp_gmf_afe.c").read_text()
    signatures = ["static void esp_gmf_afe_result_proc(",
                  "static esp_gmf_job_err_t esp_gmf_afe_proc(",
                  "esp_gmf_err_t esp_gmf_afe_set_output_callback("]
    bodies = "\n".join(function_body(source, item) for item in signatures)
    fixture = Path(__file__).with_name("afe_output_harness.cpp").read_text()
    with tempfile.TemporaryDirectory() as directory:
        directory = Path(directory)
        unit = directory / "test.cpp"
        executable = directory / "test"
        unit.write_text(fixture.replace("// INSERT_PRODUCTION_FUNCTIONS_HERE", bodies))
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-pthread",
                        str(unit), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)

if __name__ == "__main__":
    test_ready_output_does_not_wait_for_input()
