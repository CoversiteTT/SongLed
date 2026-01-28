import argparse
import sys
import time
from ctypes import POINTER, Structure, byref, c_int, c_longlong, c_uint, c_ulong, c_void_p, c_wchar_p, cast

import comtypes
from comtypes import CLSCTX_ALL, GUID, COMMETHOD, HRESULT
from comtypes.automation import PROPVARIANT
from comtypes.client import CreateObject
import serial
from serial.tools import list_ports

from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume


# ----------------------------
# COM definitions for device enumeration and default device switching
# ----------------------------
class PROPERTYKEY(Structure):
    _fields_ = [("fmtid", GUID), ("pid", c_ulong)]


PKEY_Device_FriendlyName = PROPERTYKEY(
    GUID("{a45c254e-df1c-4efd-8020-67d146a850e0}"), 14
)


class IPropertyStore(comtypes.IUnknown):
    _iid_ = GUID("{886d8eeb-8cf2-4446-8d02-cdba1dbdcf99}")
    _methods_ = [
        COMMETHOD([], HRESULT, "GetCount", (["out"], POINTER(c_uint), "cProps")),
        COMMETHOD([], HRESULT, "GetAt", (["in"], c_uint, "iProp"), (["out"], POINTER(PROPERTYKEY), "pkey")),
        COMMETHOD([], HRESULT, "GetValue", (["in"], POINTER(PROPERTYKEY), "key"), (["out"], POINTER(PROPVARIANT), "pv")),
        COMMETHOD([], HRESULT, "SetValue", (["in"], POINTER(PROPERTYKEY), "key"), (["in"], POINTER(PROPVARIANT), "pv")),
        COMMETHOD([], HRESULT, "Commit"),
    ]


class IMMDevice(comtypes.IUnknown):
    _iid_ = GUID("{D666063F-1587-4E43-81F1-B948E807363F}")
    _methods_ = [
        COMMETHOD(
            [], HRESULT, "Activate",
            (["in"], GUID, "iid"),
            (["in"], c_int, "dwClsCtx"),
            (["in"], c_void_p, "pActivationParams"),
            (["out"], POINTER(c_void_p), "ppInterface"),
        ),
        COMMETHOD([], HRESULT, "OpenPropertyStore", (["in"], c_uint, "stgmAccess"), (["out"], POINTER(POINTER(IPropertyStore)), "ppProperties")),
        COMMETHOD([], HRESULT, "GetId", (["out"], POINTER(c_wchar_p), "ppstrId")),
        COMMETHOD([], HRESULT, "GetState", (["out"], POINTER(c_uint), "pdwState")),
    ]


class IMMDeviceCollection(comtypes.IUnknown):
    _iid_ = GUID("{0BD7A1BE-7A1A-44DB-8397-C0C9A8E0B4A7}")
    _methods_ = [
        COMMETHOD([], HRESULT, "GetCount", (["out"], POINTER(c_uint), "pcDevices")),
        COMMETHOD([], HRESULT, "Item", (["in"], c_uint, "nDevice"), (["out"], POINTER(POINTER(IMMDevice)), "ppDevice")),
    ]


class IMMDeviceEnumerator(comtypes.IUnknown):
    _iid_ = GUID("{A95664D2-9614-4F35-A746-DE8DB63617E6}")
    _methods_ = [
        COMMETHOD([], HRESULT, "EnumAudioEndpoints", (["in"], c_int, "dataFlow"), (["in"], c_uint, "dwStateMask"), (["out"], POINTER(POINTER(IMMDeviceCollection)), "ppDevices")),
        COMMETHOD([], HRESULT, "GetDefaultAudioEndpoint", (["in"], c_int, "dataFlow"), (["in"], c_int, "role"), (["out"], POINTER(POINTER(IMMDevice)), "ppEndpoint")),
        COMMETHOD([], HRESULT, "GetDevice", (["in"], c_wchar_p, "pwstrId"), (["out"], POINTER(POINTER(IMMDevice)), "ppDevice")),
        COMMETHOD([], HRESULT, "RegisterEndpointNotificationCallback", (["in"], c_void_p, "pClient")),
        COMMETHOD([], HRESULT, "UnregisterEndpointNotificationCallback", (["in"], c_void_p, "pClient")),
    ]


class IPolicyConfig(comtypes.IUnknown):
    _iid_ = GUID("{F8679F50-850A-41CF-9C72-430F290290C8}")
    _methods_ = [
        COMMETHOD([], HRESULT, "GetMixFormat", (["in"], c_wchar_p, "pwstrDeviceId"), (["out"], POINTER(c_void_p), "ppFormat")),
        COMMETHOD([], HRESULT, "GetDeviceFormat", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], c_int, "bDefault"), (["out"], POINTER(c_void_p), "ppFormat")),
        COMMETHOD([], HRESULT, "ResetDeviceFormat", (["in"], c_wchar_p, "pwstrDeviceId")),
        COMMETHOD([], HRESULT, "SetDeviceFormat", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], c_void_p, "pEndpointFormat"), (["in"], c_void_p, "pMixFormat")),
        COMMETHOD([], HRESULT, "GetProcessingPeriod", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], c_int, "bDefault"), (["out"], POINTER(c_longlong), "pDefaultPeriod"), (["out"], POINTER(c_longlong), "pMinimumPeriod")),
        COMMETHOD([], HRESULT, "SetProcessingPeriod", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], POINTER(c_longlong), "pPeriod")),
        COMMETHOD([], HRESULT, "GetShareMode", (["in"], c_wchar_p, "pwstrDeviceId"), (["out"], POINTER(c_int), "pMode")),
        COMMETHOD([], HRESULT, "SetShareMode", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], c_int, "mode")),
        COMMETHOD([], HRESULT, "GetPropertyValue", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], POINTER(PROPERTYKEY), "key"), (["out"], POINTER(PROPVARIANT), "pv")),
        COMMETHOD([], HRESULT, "SetPropertyValue", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], POINTER(PROPERTYKEY), "key"), (["in"], POINTER(PROPVARIANT), "pv")),
        COMMETHOD([], HRESULT, "SetDefaultEndpoint", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], c_int, "role")),
        COMMETHOD([], HRESULT, "SetEndpointVisibility", (["in"], c_wchar_p, "pwstrDeviceId"), (["in"], c_int, "bVisible")),
    ]


CLSID_MMDeviceEnumerator = GUID("{BCDE0395-E52F-467C-8E3D-C4579291692E}")
CLSID_PolicyConfigClient = GUID("{870AF99C-171D-4F9E-AF0D-E63DF40C2BC9}")

EDataFlow_Render = 0
ERole_Console = 0
ERole_Multimedia = 1
ERole_Communications = 2
DEVICE_STATE_ACTIVE = 0x00000001
STGM_READ = 0x00000000


def safe_ascii(text):
    return "".join(ch if 32 <= ord(ch) < 127 else "?" for ch in text)


def get_device_name(device):
    props = device.OpenPropertyStore(STGM_READ)
    prop = PROPVARIANT()
    props.GetValue(byref(PKEY_Device_FriendlyName), byref(prop))
    return prop.value


def list_render_devices():
    enumerator = CreateObject(CLSID_MMDeviceEnumerator, interface=IMMDeviceEnumerator)
    collection = enumerator.EnumAudioEndpoints(EDataFlow_Render, DEVICE_STATE_ACTIVE)
    count = collection.GetCount()
    devices = []
    for i in range(count):
        dev = collection.Item(i)
        dev_id = dev.GetId()
        name = get_device_name(dev)
        devices.append({"idx": i, "id": dev_id, "name": name})
    return devices


def get_default_device_id():
    enumerator = CreateObject(CLSID_MMDeviceEnumerator, interface=IMMDeviceEnumerator)
    dev = enumerator.GetDefaultAudioEndpoint(EDataFlow_Render, ERole_Multimedia)
    return dev.GetId()


def set_default_device(device_id):
    policy = CreateObject(CLSID_PolicyConfigClient, interface=IPolicyConfig)
    policy.SetDefaultEndpoint(device_id, ERole_Console)
    policy.SetDefaultEndpoint(device_id, ERole_Multimedia)
    policy.SetDefaultEndpoint(device_id, ERole_Communications)


class AudioController:
    def __init__(self):
        self.endpoint = None
        self.refresh()

    def refresh(self):
        speakers = AudioUtilities.GetSpeakers()
        interface = speakers.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        self.endpoint = cast(interface, POINTER(IAudioEndpointVolume))

    def get_volume(self):
        return int(round(self.endpoint.GetMasterVolumeLevelScalar() * 100))

    def set_volume(self, percent):
        percent = max(0, min(100, int(percent)))
        self.endpoint.SetMasterVolumeLevelScalar(percent / 100.0, None)

    def change_volume(self, delta):
        self.set_volume(self.get_volume() + int(delta))

    def toggle_mute(self):
        current = self.endpoint.GetMute()
        self.endpoint.SetMute(0 if current else 1, None)
        return 0 if current else 1

    def get_mute(self):
        return int(self.endpoint.GetMute())


def choose_port(specified):
    if specified:
        return specified
    ports = list(list_ports.comports())
    if len(ports) == 1:
        return ports[0].device
    if not ports:
        print("No serial ports found. Check the USB cable and drivers.")
        sys.exit(1)
    auto = find_port_by_handshake([p.device for p in ports])
    if auto:
        print(f"Auto-detected device on {auto}")
        return auto
    print("Multiple ports found. Use --port.")
    for p in ports:
        print(f"  {p.device}  {p.description}")
    sys.exit(1)


def find_port_by_handshake(port_list):
    for port in port_list:
        try:
            ser = serial.Serial(port, 115200, timeout=0.1)
        except serial.SerialException:
            continue
        try:
            ser.reset_input_buffer()
            ser.write(b"HELLO\n")
            start = time.time()
            buf = ""
            while time.time() - start < 1.2:
                data = ser.read(64)
                if data:
                    buf += data.decode("utf-8", errors="ignore")
                    while "\n" in buf:
                        line, buf = buf.split("\n", 1)
                        line = line.strip()
                        if line.startswith("HELLO"):
                            ser.close()
                            return port
                else:
                    time.sleep(0.01)
        finally:
            try:
                ser.close()
            except Exception:
                pass
    return None


def main():
    parser = argparse.ArgumentParser(description="ESP32 volume + speaker bridge for Windows 11")
    parser.add_argument("--port", help="COM port, e.g. COM6")
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    comtypes.CoInitialize()
    audio = AudioController()

    port = choose_port(args.port)
    ser = serial.Serial(port, args.baud, timeout=0.05)
    time.sleep(1.2)

    def send_line(line):
        ser.write((line + "\n").encode("utf-8"))

    def send_volume():
        send_line(f"VOL {audio.get_volume()}")

    def send_mute():
        send_line(f"MUTE {audio.get_mute()}")

    def send_speaker_list():
        devices = list_render_devices()
        send_line("SPK BEGIN")
        for d in devices:
            name = safe_ascii(d["name"])
            send_line(f"SPK ITEM {d['idx']} {name}")
        send_line("SPK END")

        current_id = get_default_device_id()
        current_idx = -1
        for d in devices:
            if d["id"] == current_id:
                current_idx = d["idx"]
                break
        if current_idx >= 0:
            send_line(f"SPK CUR {current_idx}")
        return devices

    devices_cache = send_speaker_list()
    send_volume()
    send_mute()

    buf = ""
    while True:
        data = ser.read(64)
        if data:
            buf += data.decode("utf-8", errors="ignore")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                parts = line.split()
                if parts[0] == "HELLO":
                    send_line("HELLO OK")
                    continue
                if parts[0] == "VOL":
                    if len(parts) == 2 and parts[1] == "GET":
                        send_volume()
                    elif len(parts) >= 3 and parts[1] == "SET":
                        audio.set_volume(parts[2])
                        send_volume()
                    elif len(parts) >= 2:
                        try:
                            delta = int(parts[1])
                            audio.change_volume(delta)
                            send_volume()
                        except ValueError:
                            pass
                elif parts[0] == "MUTE":
                    audio.toggle_mute()
                    send_mute()
                elif parts[0] == "SPK":
                    if len(parts) >= 2 and parts[1] == "LIST":
                        devices_cache = send_speaker_list()
                    elif len(parts) >= 3 and parts[1] == "SET":
                        try:
                            idx = int(parts[2])
                        except ValueError:
                            continue
                        target = next((d for d in devices_cache if d["idx"] == idx), None)
                        if target:
                            set_default_device(target["id"])
                            audio.refresh()
                            send_speaker_list()
        else:
            time.sleep(0.01)


if __name__ == "__main__":
    main()
