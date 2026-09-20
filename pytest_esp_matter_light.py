# SPDX-License-Identifier: CC0-1.0

import os
import re
import subprocess
import time

import pytest
from gitlab_api import GitLabAPI
from pytest_cert_helper import run_python_certification_tests
from pytest_embedded import Dut

ESP_MATTER_PATH = os.environ["ESP_MATTER_PATH"]

CURRENT_DIR_LIGHT = os.path.join(ESP_MATTER_PATH, "examples", "light")
CHIP_TOOL_EXE = os.path.join(
    ESP_MATTER_PATH, "connectedhomeip", "connectedhomeip", "out", "host", "chip-tool"
)
pytest_build_dir = CURRENT_DIR_LIGHT

gitlab_api = GitLabAPI()
PYTEST_SSID = gitlab_api.ci_gitlab_pytest_ssid
PYTEST_PASSPHRASE = gitlab_api.ci_gitlab_pytest_passphrase


@pytest.mark.esp32c3
@pytest.mark.esp_matter_dut
@pytest.mark.parametrize(
    " count, app_path, target, erase_all",
    [
        (1, pytest_build_dir, "esp32c3", "y"),
    ],
    indirect=True,
)

# Matter over wifi commissioning
def test_matter_commissioning_c3(dut: Dut) -> None:
    light = dut
    # BLE start advertising
    light.expect(r"Configuring CHIPoBLE advertising", timeout=20)
    # Start commissioning
    time.sleep(5)
    command = (
        CHIP_TOOL_EXE
        + f" pairing ble-wifi 1 {PYTEST_SSID} {PYTEST_PASSPHRASE} 20202021 3840"
    )
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False
    # Use toggle command to turn-off the light
    time.sleep(3)
    command = CHIP_TOOL_EXE + " onoff toggle 1 1"
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False
    # Use toggle command to turn-on the light
    time.sleep(5)
    command = CHIP_TOOL_EXE + " onoff toggle 1 1"
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False


@pytest.mark.esp32c2
@pytest.mark.esp_matter_dut
@pytest.mark.parametrize(
    " count, app_path, target, erase_all",
    [
        (1, pytest_build_dir, "esp32c2", "y"),
    ],
    indirect=True,
)

# Matter over wifi commissioning
def test_matter_commissioning_c2(dut: Dut) -> None:
    light = dut
    # BLE start advertising
    light.expect(r"Configuring CHIPoBLE advertising", timeout=20)
    # Start commissioning
    time.sleep(5)
    command = (
        CHIP_TOOL_EXE
        + f" pairing ble-wifi 1 {PYTEST_SSID} {PYTEST_PASSPHRASE} 20202021 3840"
    )
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False
    # Use toggle command to turn-off the light
    time.sleep(3)
    command = CHIP_TOOL_EXE + " onoff toggle 1 1"
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False
    # Use toggle command to turn-on the light
    time.sleep(5)
    command = CHIP_TOOL_EXE + " onoff toggle 1 1"
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False


@pytest.mark.esp32c6
@pytest.mark.esp_matter_dut
@pytest.mark.parametrize(
    " count, app_path, target, erase_all",
    [
        (1, pytest_build_dir, "esp32c6", "y"),
    ],
    indirect=True,
)

# Matter over wifi commissioning
def test_matter_commissioning_c6(dut: Dut) -> None:
    light = dut
    # BLE start advertising
    light.expect(r"Configuring CHIPoBLE advertising", timeout=20)
    # Start commissioning
    time.sleep(5)
    command = (
        CHIP_TOOL_EXE
        + f" pairing ble-wifi 1 {PYTEST_SSID} {PYTEST_PASSPHRASE} 20202021 3840"
    )
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False
    # Use toggle command to turn-off the light
    time.sleep(3)
    command = CHIP_TOOL_EXE + " onoff toggle 1 1"
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False
    # Use toggle command to turn-on the light
    time.sleep(5)
    command = CHIP_TOOL_EXE + " onoff toggle 1 1"
    out_str = subprocess.getoutput(command)
    print(out_str)
    result = re.findall(r"Run command failure", str(out_str))
    if len(result) != 0:
        assert False


@pytest.mark.esp32c6
@pytest.mark.esp_matter_certification
@pytest.mark.parametrize(
    " count, app_path, target, erase_all",
    [
        (1, pytest_build_dir, "esp32c6", "y"),
    ],
    indirect=True,
)
def test_matter_certification_c6(
    dut: Dut, certification_tests: str, ci_branch: str
) -> None:
    dut.expect(r"Configuring CHIPoBLE advertising", timeout=20)
    time.sleep(5)
    run_python_certification_tests(dut, certification_tests, ci_branch)
